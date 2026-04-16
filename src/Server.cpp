#include "GlobalData.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include "httplib.h"
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

using json = nlohmann::json;

void http_server_func(GlobalData* globalData) {
    httplib::Server svr;
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        const std::string html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Signal Map НСК</title>
        <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
        <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
        <style>
            #map { height: 100vh; width: 100%; margin: 0; }
            .leaflet-popup-content { font-family: sans-serif; line-height: 1.4; font-size: 14px; }
            .leaflet-popup-content b { color: #333; }
        </style>
    </head>
    <body>
        <div id="map"></div>
        <script>
            var map = L.map('map').setView([55.03, 82.92], 13);
            L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
                attribution: '&copy; OpenStreetMap contributors'
            }).addTo(map);

            var noShadowIcon = L.icon({
                iconUrl: 'https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon.png',
                iconRetinaUrl: 'https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon-2x.png',
                iconSize: [25, 41],
                iconAnchor: [12, 41],
                popupAnchor: [1, -34]
            });

            var markerLayer = L.layerGroup().addTo(map);
            var lastPointCount = 0;

            function update() {
                fetch('/data')
                    .then(r => r.json())
                    .then(points => {
                        if (points.length > lastPointCount) {
                            markerLayer.clearLayers();
                            points.forEach(p => {
                                var marker = L.marker([p.lat, p.lon], { icon: noShadowIcon }).addTo(markerLayer);
                                var popupContent = `<b>Время:</b> ${p.time}<br><b>RSRP:</b> ${p.sig} dBm<br><b>GPS:</b> ${p.lat.toFixed(6)}, ${p.lon.toFixed(6)}`;
                                marker.bindPopup(popupContent);
                            });
                            if (lastPointCount === 0 && points.length > 0) {
                                var last = points[points.length - 1];
                                map.setView([last.lat, last.lon], 15);
                            }
                            lastPointCount = points.length;
                        }
                    })
                    .catch(err => console.error('Error:', err));
            }
            setInterval(update, 3000);
            update();
        </script>
    </body>
    </html>)";
        res.set_content(html, "text/html; charset=utf-8");
    });

    svr.Get("/data", [globalData](const httplib::Request&, httplib::Response& res) {
        auto info = globalData->get();
        json j = json::array();
        for(auto& p : info.history) {
            j.push_back({{"lat", p.lat}, {"lon", p.lon}, {"sig", p.signal}, {"time", p.time_str}});
        }
        res.set_content(j.dump(), "application/json");
    });

    std::cout << "[HTTP] Server started on http://0.0.0.0:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);
}

void zmq_server_func(GlobalData* globalData) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.bind("tcp://0.0.0.0:5554");
    
    std::cout << "[SYSTEM] Ожидание данных от телефона по ZMQ или файла history.json..." << std::endl;

    while (true) {
        zmq::message_t req;
        auto res = socket.recv(req, zmq::recv_flags::dontwait);

        if (res) {
            std::string msg(static_cast<char*>(req.data()), req.size());
            globalData->update(msg);
            socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
        } else {
            if (std::filesystem::exists("history.json")) {
                std::cout << "[SYSTEM] Телефон молчит. Начинаем обработку history.json..." << std::endl;
                try {
                    std::ifstream f("history.json");
                    json data = json::parse(f);

                    if (data.is_array()) {
                        for (auto& entry : data) {
                            globalData->update(entry.dump());
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }
                    std::filesystem::rename("history.json", "history.json.processed");
                    std::cout << "[SYSTEM] history.json обработан и сохранен!" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[FILE ERROR] Ошибка чтения history.json: " << e.what() << std::endl;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}