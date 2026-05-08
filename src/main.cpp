#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <thread>
#include "GlobalData.h"
#include <curl/curl.h>
#include <map>
#include <queue>
#include <mutex>
#include <cmath>
#include <filesystem>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h" 

std::mutex g_StatusMutex;
std::string g_HeatmapStatus = "Waiting...";

struct TileJob { std::string id; int zoom, x, y; };

struct TextureData {
    GLuint id = 0;
    bool isLoading = false;
    std::vector<uint8_t> rgbaBlob;
    int width = 0, height = 0;
};

struct HeatmapTexture {
    GLuint id = 0;
    bool needsUpload = false;
    std::vector<uint8_t> rgbaBlob;
};

std::map<std::string, TextureData> g_TileCache;
std::map<std::string, HeatmapTexture> g_HeatmapCache;

std::queue<TileJob> g_JobQueue;
std::mutex g_JobMutex;
std::mutex g_CacheMutex;
std::mutex g_HeatmapMutex;
int g_CurrentZoom = 13;

const double PI = 3.14159265358979323846;

double MercatorXToTileX(double mercatorX, int zoom) { return (0.5 + mercatorX / 360.0) * (1 << zoom); }
double MercatorYToTileY(double mercatorY, int zoom) { return (0.5 - mercatorY / 360.0) * (1 << zoom); }
double TileXToMercatorX(int tileX, int zoom) { return (tileX / static_cast<double>(1 << zoom) - 0.5) * 360.0; }
double TileYToMercatorY(int tileY, int zoom) { return (0.5 - tileY / static_cast<double>(1 << zoom)) * 360.0; }
double LatToMercatorY(double lat) {
    double rad = lat * PI / 180.0;
    return (std::log(std::tan(rad) + 1.0 / std::cos(rad))) * 180.0 / PI;
}

double FastDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * 111320.0; 
    double avgLat = (lat1 + lat2) / 2.0;
    double dLon = (lon2 - lon1) * 111320.0 * std::cos(avgLat * PI / 180.0);
    return std::sqrt(dLat * dLat + dLon * dLon);
}

void PixelToLatLon(int tileX, int tileY, int zoom, int px, int py, double& lat, double& lon) {
    double n = std::pow(2.0, zoom);
    lon = (tileX + px / 256.0) / n * 360.0 - 180.0;
    double lat_rad = std::atan(std::sinh(PI * (1 - 2 * (tileY + py / 256.0) / n)));
    lat = lat_rad * 180.0 / PI;
}

struct ColorRGBA { unsigned char r, g, b, a; };

ColorRGBA GetRsrpColor(float rsrp) {
    if (rsrp > -80)  return {255, 0, 0, 180};     
    if (rsrp > -90)  return {255, 165, 0, 150};   
    if (rsrp > -100) return {255, 255, 0, 120};   
    if (rsrp > -110) return {0, 0, 255, 100};     
    return {0, 0, 0, 0};                          
}

float CalculateIDW(double targetLat, double targetLon, const std::vector<SignalPoint>& history, float maxRadiusMeters) {
    double numerator = 0.0, denominator = 0.0;
    bool hasData = false; double p = 2.0; 
    for (const auto& pt : history) {
        double dist = FastDistanceMeters(targetLat, targetLon, pt.lat, pt.lon);
        if (dist <= maxRadiusMeters) {
            if (dist < 1.0) dist = 1.0; 
            double weight = 1.0 / std::pow(dist, p);
            numerator += weight * pt.signal; denominator += weight; hasData = true;
        }
    }
    if (!hasData) return -200.0f; 
    return static_cast<float>(numerator / denominator);
}

void GenerateHeatmapTile(int zoom, int x, int y, const std::vector<SignalPoint>& history, float radius) {
    int w = 64, h = 64, channels = 4;
    std::vector<unsigned char> image(w * h * channels, 0); 
    
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double pixelLat, pixelLon;
            PixelToLatLon(x, y, zoom, px * 4, py * 4, pixelLat, pixelLon);
            float signal = CalculateIDW(pixelLat, pixelLon, history, radius);
            if (signal > -150.0f) { 
                ColorRGBA color = GetRsrpColor(signal);
                int idx = (py * w + px) * channels;
                image[idx] = color.r; image[idx+1] = color.g; image[idx+2] = color.b; image[idx+3] = color.a;
            }
        }
        if (py % 16 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::string dir = "build/" + std::to_string(zoom) + "/" + std::to_string(x);
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    std::string filename = dir + "/" + std::to_string(y) + ".png";
    stbi_write_png(filename.c_str(), w, h, channels, image.data(), w * channels);

    std::string tileId = std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y);
    {
        std::lock_guard<std::mutex> lock(g_HeatmapMutex);
        g_HeatmapCache[tileId].rgbaBlob = image;
        g_HeatmapCache[tileId].needsUpload = true;
    }
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void FetchWorker() {
    CURL* curl = curl_easy_init();
    while (true) {
        TileJob job;
        {
            std::lock_guard<std::mutex> lock(g_JobMutex);
            if (g_JobQueue.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            job = g_JobQueue.front(); g_JobQueue.pop();
        }
        std::string url = "https://tile.openstreetmap.org/" + std::to_string(job.zoom) + "/" + std::to_string(job.x) + "/" + std::to_string(job.y) + ".png";
        std::string readBuffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SignalMonitor/2.0 (C++ Client)");

        if (curl_easy_perform(curl) == CURLE_OK) {
            int w, h, c;
            unsigned char* data = stbi_load_from_memory((unsigned char*)readBuffer.data(), readBuffer.size(), &w, &h, &c, 4);
            if (!data) { 
                data = (unsigned char*)malloc(4); 
                data[0]=0; data[1]=0; data[2]=0; data[3]=0; 
                w=1; h=1; 
            }
            
            std::lock_guard<std::mutex> lock(g_CacheMutex);
            g_TileCache[job.id].rgbaBlob.assign(data, data + (w * h * 4));
            g_TileCache[job.id].width = w; g_TileCache[job.id].height = h; g_TileCache[job.id].isLoading = false;
            stbi_image_free(data);
        } else {
            std::lock_guard<std::mutex> lock(g_CacheMutex);
            g_TileCache.erase(job.id); 
        }
    }
    curl_easy_cleanup(curl);
}

void zmq_server_func(GlobalData* globalData);
void http_server_func(GlobalData* globalData);

void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "[GLFW ERROR %d]: %s\n", error, description);
}

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return -1;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Signal Monitor", NULL, NULL);
    if (!window) return -1;
    
    glfwMaximizeWindow(window);
    glfwShowWindow(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ImGui::GetStyle().AntiAliasedLines = false;
    ImGui::GetStyle().AntiAliasedFill = false;

    GlobalData data;
    std::thread(zmq_server_func, &data).detach();
    std::thread(http_server_func, &data).detach();
    std::thread(FetchWorker).detach();

    bool autoScale = true, autoScaleMap = true, triggerHeatmap = false; 
    float heatmapRadius = 500.0f;
    int selectedCriterion = 0;
    const char* criteria[] = { "RSRP", "RSRQ", "RSSI", "Altitude" };

    static std::vector<double> routeCacheX, routeCacheY;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        auto info = data.get();

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({350, ImGui::GetIO().DisplaySize.y});
        ImGui::Begin("Control Panel", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
        
        ImGui::Text("Signal: %.0f dBm", info.currentSignal);
        ImGui::TextColored(ImVec4(1,1,0,1), "GPS: %.6f, %.6f", info.lat, info.lon);
        ImGui::Separator();

        ImGui::Text("Graph Settings");
        if (ImGui::Button(autoScale ? "Auto-Scale Graph: ON" : "Auto-Scale Graph: OFF", ImVec2(-1, 30))) autoScale = !autoScale;
        if (ImGui::Button(autoScaleMap ? "Auto-Scale Map: ON" : "Auto-Scale Map: OFF", ImVec2(-1, 30))) autoScaleMap = !autoScaleMap;
        if (ImGui::Button("Reset View", ImVec2(-1, 30))) { autoScale = true; autoScaleMap = true; }
        ImGui::Separator();

        ImGui::Text("Heatmap Generator (IDW)");
        ImGui::Combo("Criterion", &selectedCriterion, criteria, IM_ARRAYSIZE(criteria));
        ImGui::SliderFloat("Radius (m)", &heatmapRadius, 50.0f, 5000.0f);
        if (ImGui::Button("Generate Heatmap", ImVec2(-1, 40))) triggerHeatmap = true;

        std::string currentStatus;
        { std::lock_guard<std::mutex> lock(g_StatusMutex); currentStatus = g_HeatmapStatus; }
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", currentStatus.c_str());
        ImGui::End();

        ImGui::SetNextWindowPos({350, 0});
        ImGui::SetNextWindowSize({ImGui::GetIO().DisplaySize.x - 350, ImGui::GetIO().DisplaySize.y});
        ImGui::Begin("Monitors", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

        if (info.isConnected || !info.history.empty()) {
            if (ImPlot::BeginPlot("##Signal", ImVec2(-1, ImGui::GetContentRegionAvail().y / 2.5f))) {
                ImPlot::SetupAxes("Timeline", "RSRP (dBm)");
                if (autoScale) {
                    ImPlot::SetupAxisLimits(ImAxis_X1, info.timeSec - 60, info.timeSec, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, info.currentSignal - 15, info.currentSignal + 15, ImGuiCond_Always);
                }
                if (!info.timeData.empty()) ImPlot::PlotLine("RSRP", info.timeData.data(), info.signalData.data(), info.timeData.size());
                if (ImPlot::IsPlotHovered() && (ImGui::GetIO().MouseWheel != 0 || ImGui::IsMouseDown(0))) autoScale = false;
                ImPlot::EndPlot();
            }

            if (ImPlot::BeginPlot("##Map", ImVec2(-1, -1), ImPlotFlags_Equal)) {
                ImPlot::SetupAxes("Longitude", "Latitude (Mercator)", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                
                ImVec2 avail = ImGui::GetContentRegionAvail();
                double aspect = avail.y > 0 ? (double)avail.x / (double)avail.y : 1.0;
                double radiusY = 0.02;
                double radiusX = radiusY * aspect; 
                
                if (autoScaleMap && !info.history.empty()) {
                    double my = LatToMercatorY(info.lat);
                    ImPlot::SetupAxisLimits(ImAxis_X1, info.lon - radiusX, info.lon + radiusX, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, my - radiusY, my + radiusY, ImGuiCond_Always);
                }

                ImPlotRect limits = ImPlot::GetPlotLimits();
                double dx = limits.X.Max - limits.X.Min;
                
                int zoom = 4;
                if (dx > 90.0) zoom = 4; else if (dx > 45.0) zoom = 5; else if (dx > 22.5) zoom = 6;
                else if (dx > 11.2) zoom = 7; else if (dx > 5.6) zoom = 8; else if (dx > 2.8) zoom = 9;
                else if (dx > 1.4) zoom = 10; else if (dx > 0.7) zoom = 11; else if (dx > 0.35) zoom = 12;
                else if (dx > 0.17) zoom = 13; else if (dx > 0.08) zoom = 14; else if (dx > 0.04) zoom = 15;
                else if (dx > 0.02) zoom = 16; else if (dx > 0.01) zoom = 17; else if (dx > 0.005) zoom = 18; else zoom = 19;

                if (zoom != g_CurrentZoom) {
                    g_CurrentZoom = zoom;
                    { 
                        std::lock_guard<std::mutex> lock(g_JobMutex); 
                        std::queue<TileJob> empty; std::swap(g_JobQueue, empty); 
                    }
                    {
                        std::lock_guard<std::mutex> lock(g_CacheMutex);
                        for (auto& pair : g_TileCache) { if (pair.second.id != 0) glDeleteTextures(1, &pair.second.id); }
                        g_TileCache.clear();
                    }
                    {
                        std::lock_guard<std::mutex> lock(g_HeatmapMutex);
                        for (auto& pair : g_HeatmapCache) { if (pair.second.id != 0) glDeleteTextures(1, &pair.second.id); }
                        g_HeatmapCache.clear();
                    }
                }

                int minX = std::max(0, (int)std::floor(MercatorXToTileX(limits.X.Min, zoom)));
                int minY = std::max(0, (int)std::floor(MercatorYToTileY(limits.Y.Max, zoom)));
                int maxX = std::min((1<<zoom)-1, (int)std::floor(MercatorXToTileX(limits.X.Max, zoom)));
                int maxY = std::min((1<<zoom)-1, (int)std::floor(MercatorYToTileY(limits.Y.Min, zoom)));

                if (maxX - minX > 8) { int midX = (minX + maxX)/2; minX = midX - 4; maxX = midX + 4; }
                if (maxY - minY > 8) { int midY = (minY + maxY)/2; minY = midY - 4; maxY = midY + 4; }
                minX = std::max(0, minX); maxX = std::min((1<<zoom)-1, maxX);
                minY = std::max(0, minY); maxY = std::min((1<<zoom)-1, maxY);

                if (triggerHeatmap) {
                    triggerHeatmap = false;
                    std::vector<SignalPoint> currentHistory = info.history; 
                    int z = zoom; float r = heatmapRadius;
                    
                    std::thread([=]() {
                        int total = (maxX - minX + 1) * (maxY - minY + 1), count = 0;
                        for (int tx = minX; tx <= maxX; ++tx) {
                            for (int ty = minY; ty <= maxY; ++ty) {
                                count++;
                                { std::lock_guard<std::mutex> lock(g_StatusMutex); g_HeatmapStatus = "Calculating... (" + std::to_string(count) + "/" + std::to_string(total) + ")"; }
                                GenerateHeatmapTile(z, tx, ty, currentHistory, r);
                            }
                        }
                        { std::lock_guard<std::mutex> lock(g_StatusMutex); g_HeatmapStatus = "Success! Rendered ON MAP."; }
                    }).detach();
                }

                int texturesUploadedThisFrame = 0; 
                int maxUploads = 2;

                for (int x = minX; x <= maxX; ++x) {
                    for (int y = minY; y <= maxY; ++y) {
                        std::string tileId = std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y);
                        GLuint gpuId = 0; bool needLoad = false;
                        {
                            std::lock_guard<std::mutex> lock(g_CacheMutex);
                            if (g_TileCache.find(tileId) == g_TileCache.end()) {
                                g_TileCache[tileId].isLoading = true; needLoad = true;
                            } else {
                                auto& tex = g_TileCache[tileId];
                                if (!tex.rgbaBlob.empty() && texturesUploadedThisFrame < maxUploads) {
                                    glGenTextures(1, &tex.id); glBindTexture(GL_TEXTURE_2D, tex.id);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); 
                                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.rgbaBlob.data());
                                    tex.rgbaBlob.clear();
                                    texturesUploadedThisFrame++;
                                }
                                gpuId = tex.id;
                            }
                        }
                        if (needLoad) { 
                            std::lock_guard<std::mutex> lock(g_JobMutex); 
                            g_JobQueue.push({tileId, zoom, x, y}); 
                        }
                        if (gpuId != 0) {
                            ImPlotPoint minP{ TileXToMercatorX(x, zoom), TileYToMercatorY(y + 1, zoom) };
                            ImPlotPoint maxP{ TileXToMercatorX(x + 1, zoom), TileYToMercatorY(y, zoom) };
                            ImPlot::PlotImage(("##" + tileId).c_str(), (ImTextureID)(intptr_t)gpuId, minP, maxP);
                        }
                    }
                }

                for (int x = minX; x <= maxX; ++x) {
                    for (int y = minY; y <= maxY; ++y) {
                        std::string tileId = std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y);
                        GLuint hmGpuId = 0;
                        {
                            std::lock_guard<std::mutex> lock(g_HeatmapMutex);
                            auto it = g_HeatmapCache.find(tileId);
                            if (it != g_HeatmapCache.end()) {
                                if (it->second.needsUpload && texturesUploadedThisFrame < maxUploads) {
                                    glGenTextures(1, &it->second.id);
                                    glBindTexture(GL_TEXTURE_2D, it->second.id);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); 
                                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, it->second.rgbaBlob.data());
                                    it->second.rgbaBlob.clear();
                                    it->second.needsUpload = false;
                                    texturesUploadedThisFrame++;
                                }
                                hmGpuId = it->second.id;
                            }
                        }
                        if (hmGpuId != 0) {
                            ImPlotPoint minP{ TileXToMercatorX(x, zoom), TileYToMercatorY(y + 1, zoom) };
                            ImPlotPoint maxP{ TileXToMercatorX(x + 1, zoom), TileYToMercatorY(y, zoom) };
                            ImPlot::PlotImage(("##hm_" + tileId).c_str(), (ImTextureID)(intptr_t)hmGpuId, minP, maxP);
                        }
                    }
                }

                if (!info.history.empty()) {
                    if (routeCacheX.size() < info.history.size()) {
                        routeCacheX.reserve(info.history.size());
                        routeCacheY.reserve(info.history.size());
                        for (size_t i = routeCacheX.size(); i < info.history.size(); ++i) {
                            routeCacheX.push_back(info.history[i].lon);
                            routeCacheY.push_back(LatToMercatorY(info.history[i].lat));
                        }
                    } else if (routeCacheX.size() > info.history.size()) {
                        routeCacheX.clear();
                        routeCacheY.clear();
                    }
                    ImPlot::PlotLine("Route", routeCacheX.data(), routeCacheY.data(), routeCacheX.size());
                    ImPlot::PlotScatter("Current", &routeCacheX.back(), &routeCacheY.back(), 1);
                }
                
                if (ImPlot::IsPlotHovered() && (ImGui::GetIO().MouseWheel != 0 || ImGui::IsMouseDown(0))) autoScaleMap = false;
                ImPlot::EndPlot();
            }
        } else {
            ImGui::Text("WAITING FOR DATA...");
        }
        ImGui::End();

        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext(); ImGui::DestroyContext();
    glfwDestroyWindow(window); glfwTerminate();
    return 0;
}