/*
 SS-ELRS Healthcare / IoMT Workload Generator - C++17
 ------------------------------------------------------------
 Purpose:
  Generate realistic healthcare and IoMT SQL workloads for semantic SS-ELRS evaluation.

 Compile:
   g++ -std=c++17 -O2 healthcare_workload_generator.cpp -o healthcare_workload_generator

 Run:
   ./healthcare_workload_generator

 Output:
   healthcare_queries_100.txt
   healthcare_queries_500.txt
   healthcare_queries_1000.txt
*/

#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;

string pick(const vector<string>& v, mt19937& rng) {
    uniform_int_distribution<int> dist(0, (int)v.size() - 1);
    return v[dist(rng)];
}

void generate_workload(const string& filename, int n) {
    mt19937 rng(2026 + n);

    vector<string> diagnoses = {
        "diabetes", "hypertension", "arrhythmia", "cardiac_risk", "respiratory_distress"
    };

    vector<string> deviceTypes = {
        "ecg_monitor", "heart_rate_sensor", "glucose_sensor", "blood_pressure_sensor", "oxygen_sensor"
    };

    vector<string> alertLevels = {
        "LOW", "MEDIUM", "HIGH", "CRITICAL"
    };

    vector<string> departments = {
        "cardiology", "endocrinology", "emergency", "icu", "remote_monitoring"
    };

    ofstream out(filename);
    if (!out) {
        cerr << "Cannot create " << filename << "\n";
        return;
    }

    for (int i = 1; i <= n; ++i) {
        int pattern = i % 12;

        int patientId = 1000 + (i * 17) % 50000;
        int heartRate = 60 + (i * 7) % 90;
        int glucose = 70 + (i * 11) % 220;
        int systolic = 90 + (i * 5) % 90;
        int oxygen = 82 + (i * 3) % 18;
        int riskScore = (i * 13) % 100;
        int sensorId = 500 + (i * 19) % 10000;

        if (pattern == 0) {
            out << "SELECT patient_id, diagnosis, glucose_level, insulin_status "
                << "FROM medical_records "
                << "WHERE diagnosis = 'diabetes' AND glucose_level > " << glucose << ";\n";
        } else if (pattern == 1) {
            out << "SELECT patient_id, blood_pressure, diagnosis, risk_score "
                << "FROM medical_records "
                << "WHERE diagnosis = 'hypertension' AND blood_pressure > " << systolic << ";\n";
        } else if (pattern == 2) {
            out << "SELECT sensor_id, patient_id, ecg_signal, heart_rate "
                << "FROM iomt_sensor_data "
                << "WHERE heart_rate > " << heartRate << " AND ecg_signal IS NOT NULL;\n";
        } else if (pattern == 3) {
            out << "SELECT patient_id, heart_rate, cardiac_status "
                << "FROM ecg_records "
                << "WHERE diagnosis = 'arrhythmia' AND heart_rate > " << heartRate << ";\n";
        } else if (pattern == 4) {
            out << "SELECT patient_id, oxygen_saturation, alert_level "
                << "FROM vital_signs "
                << "WHERE oxygen_saturation < " << oxygen << " AND alert_level = '" << pick(alertLevels, rng) << "';\n";
        } else if (pattern == 5) {
            out << "SELECT p.patient_id, p.department, m.diagnosis, m.risk_score "
                << "FROM patients p JOIN medical_records m ON p.patient_id = m.patient_id "
                << "WHERE p.department = '" << pick(departments, rng) << "' AND m.risk_score > " << riskScore << ";\n";
        } else if (pattern == 6) {
            out << "SELECT d.device_id, d.device_type, s.heart_rate, s.ecg_signal "
                << "FROM iomt_devices d JOIN iomt_sensor_data s ON d.device_id = s.sensor_id "
                << "WHERE d.device_type = '" << pick(deviceTypes, rng) << "' AND s.heart_rate > " << heartRate << ";\n";
        } else if (pattern == 7) {
            out << "SELECT patient_id, glucose_level, blood_pressure, risk_score "
                << "FROM medical_records "
                << "WHERE glucose_level > " << glucose << " AND blood_pressure > " << systolic << ";\n";
        } else if (pattern == 8) {
            out << "SELECT patient_id, ecg_signal, heart_rate, alert_level "
                << "FROM ecg_records "
                << "WHERE heart_rate > " << heartRate << " AND alert_level = 'CRITICAL';\n";
        } else if (pattern == 9) {
            out << "SELECT sensor_id, patient_id, monitoring_status, timestamp_metadata "
                << "FROM iomt_sensor_data "
                << "WHERE sensor_id = " << sensorId << " AND monitoring_status = 'ACTIVE';\n";
        } else if (pattern == 10) {
            out << "SELECT patient_id, diagnosis, oxygen_saturation, respiratory_rate "
                << "FROM vital_signs "
                << "WHERE diagnosis = 'respiratory_distress' AND oxygen_saturation < " << oxygen << ";\n";
        } else {
            out << "SELECT p.patient_id, m.diagnosis, v.heart_rate, v.blood_pressure "
                << "FROM patients p JOIN medical_records m ON p.patient_id = m.patient_id "
                << "JOIN vital_signs v ON p.patient_id = v.patient_id "
                << "WHERE m.diagnosis = '" << pick(diagnoses, rng)
                << "' AND v.heart_rate > " << heartRate << ";\n";
        }
    }

    out.close();
    cout << "Generated " << filename << " with " << n << " healthcare queries.\n";
}

int main() {
    generate_workload("healthcare_queries_100.txt", 100);
    generate_workload("healthcare_queries_500.txt", 500);
    generate_workload("healthcare_queries_1000.txt", 1000);

    cout << "Done.\n";
    return 0;
}