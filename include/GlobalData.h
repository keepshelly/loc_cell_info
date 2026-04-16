#pragma once
#include <mutex>
#include <vector>
#include <pqxx/pqxx>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <regex>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

struct SignalPoint {
    float lat, lon, signal;
    std::string time_str; 
};

struct GpsInfo {
    float lat = 0.0f;
    float lon = 0.0f;
    float currentSignal = -120.0f;
    
    std::vector<float> timeData;    
    std::vector<float> signalData;  
    std::vector<SignalPoint> history; 
    
    float timeSec = 0.0f;
    bool isConnected = false;
};

class GlobalData {
    std::mutex mtx;
    GpsInfo info;
    std::string conn_str = "host=localhost port=5432 dbname=postgres user=kirill password=1234";

public:
    GlobalData() {
        try {
            pqxx::connection c(conn_str);
            pqxx::work txn(c);
            
            txn.exec("CREATE TABLE IF NOT EXISTS signals ("
                     "id SERIAL PRIMARY KEY, "
                     "lat DOUBLE PRECISION, "
                     "lon DOUBLE PRECISION, "
                     "signal REAL, "
                     "time TIMESTAMP DEFAULT CURRENT_TIMESTAMP);");
            txn.commit();

            std::cout << "[DB SYSTEM] Таблица signals проверена/создана." << std::endl;
            loadFromDb();
        } catch (const std::exception &e) {
            std::cerr << "[DB ERROR В КОНСТРУКТОРЕ] " << e.what() << std::endl;
        }
    }

    void loadFromDb() {
        try {
            pqxx::connection c(conn_str);
            pqxx::work txn(c);
            pqxx::result r = txn.exec("SELECT lat, lon, signal, time::text FROM signals ORDER BY id ASC");
            
            for (auto row : r) {
                float lat = row[0].as<float>();
                float lon = row[1].as<float>();
                float sig = row[2].as<float>();
                std::string t_str = row[3].is_null() ? "Unknown" : row[3].c_str();
                
                info.history.push_back({lat, lon, sig, t_str});
                info.timeSec += 1.0f;
                info.timeData.push_back(info.timeSec);
                info.signalData.push_back(sig);
            }
            if (!r.empty()) {
                std::cout << "[DB] Загружено " << r.size() << " точек из базы данных." << std::endl;
                info.lat = info.history.back().lat;
                info.lon = info.history.back().lon;
                info.currentSignal = info.history.back().signal;
            }
        } catch (const std::exception &e) {
            std::cerr << "[DB LOAD ERROR] " << e.what() << std::endl;
        }
    }

    void update(const std::string& raw_json) {
        std::lock_guard<std::mutex> lock(mtx);
        try {
            auto j = json::parse(raw_json);
            float lat, lon, signal = -120.0f;
            std::string time_str = "";

            if (j.contains("location")) {
                lat = j["location"]["lat"];
                lon = j["location"]["lon"];
                signal = j["telephony_data"][0]["CellInfoLte"]["CellSignalStrengthLte"]["RSRP"];
            } 
            else if (j.contains("latitude")) {
                lat = j["latitude"];
                lon = j["longitude"];
                
                std::string cellInfo = j["cellInfo"];
                std::regex rssi_regex("rssi=(-[0-9]+)");
                std::smatch match;
                if (std::regex_search(cellInfo, match, rssi_regex)) {
                    signal = std::stof(match[1].str());
                }
                
                if (j.contains("time") && j["time"].is_string()) time_str = j["time"];
                else if (j.contains("timestamp") && j["timestamp"].is_string()) time_str = j["timestamp"];
            } else {
                return;
            }

            if (time_str.empty()) {
                auto now = std::chrono::system_clock::now();
                std::time_t now_c = std::chrono::system_clock::to_time_t(now);
                char buf[100];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
                time_str = std::string(buf);
            }

            info.lat = lat;
            info.lon = lon;
            info.currentSignal = signal;
            info.isConnected = true;
            
            info.history.push_back({lat, lon, signal, time_str});
            info.timeSec += 1.0f;
            info.timeData.push_back(info.timeSec);
            info.signalData.push_back(signal);

            if(info.timeData.size() > 1000) {
                info.timeData.erase(info.timeData.begin());
                info.signalData.erase(info.signalData.begin());
            }

            pqxx::connection c(conn_str);
            pqxx::work txn(c);
            txn.exec_params("INSERT INTO signals (lat, lon, signal) VALUES ($1, $2, $3)", 
                            (double)lat, (double)lon, (float)signal);
            txn.commit();

        } catch (const std::exception& e) {
            std::cerr << "[UPDATE ERROR] " << e.what() << std::endl;
        }
    }

    GpsInfo get() {
        std::lock_guard<std::mutex> lock(mtx);
        return info;
    }
};