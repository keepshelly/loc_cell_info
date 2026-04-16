#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <thread>
#include "GlobalData.h"

void zmq_server_func(GlobalData* globalData);
void http_server_func(GlobalData* globalData);

int main() {
    if (!glfwInit()) return -1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Signal Monitor", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGui::GetIO().FontGlobalScale = 2.0f;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    GlobalData data;
    std::thread t1(zmq_server_func, &data);
    std::thread t2(http_server_func, &data);
    t1.detach(); 
    t2.detach();

    bool autoScale = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Monitor", nullptr, ImGuiWindowFlags_NoDecoration);

        auto info = data.get();
        
        if (info.isConnected || !info.history.empty()) {
            ImGui::Text("RSRP: %.0f dBm", info.currentSignal);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "| Lat: %.6f", info.lat);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "| Lon: %.6f", info.lon);
            
            ImGui::SameLine();
            if (ImGui::Button(autoScale ? "Auto-Scale: ON" : "Auto-Scale: OFF")) {
                autoScale = !autoScale;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Reset View")) {
                autoScale = true;
            }

            if (ImPlot::BeginPlot("##SignalStream", ImVec2(-1, -1))) {
                ImPlot::SetupAxes("Timeline (s)", "Signal (dBm)");
                
                if (autoScale) {
                    ImPlot::SetupAxisLimits(ImAxis_X1, info.timeSec - 60, info.timeSec, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, info.currentSignal - 15, info.currentSignal + 15, ImGuiCond_Always);
                }

                if (!info.timeData.empty()) {
                    ImPlot::PlotLine("RSRP", info.timeData.data(), info.signalData.data(), (int)info.timeData.size());
                    
                    char coords[64];
                    sprintf(coords, "GPS: %.6f, %.6f", info.lat, info.lon);
                    ImPlot::PlotText(coords, info.timeSec, info.currentSignal, ImVec2(15, -15));
                }
                
                if (ImPlot::IsPlotHovered() && ImGui::GetIO().MouseWheel != 0) {
                    autoScale = false;
                }
                ImPlot::EndPlot();
            }
        } else {
            ImGui::Text("WAITING FOR DATA FROM MOBILE OR history.json...");
        }

        ImGui::End();
        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}