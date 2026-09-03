#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "mv3dlp_laser_profile/driver.hpp"

int main(int argc, char** argv) {
    try {
        mv3dlp::DriverOptions options;
        if (argc > 1) {
            options.library_path = argv[1];
        }

        mv3dlp::Driver driver(options);
        std::cout << "SDK version: " << driver.sdkVersion() << '\n';
        std::cout << "Loaded library: " << driver.loadedLibraryPath() << '\n';

        const auto devices = driver.enumerateDevices();
        if (devices.empty()) {
            std::cerr << "No line laser devices found." << '\n';
            return 1;
        }

        std::cout << "Found " << devices.size() << " device(s)." << '\n';
        for (std::size_t index = 0; index < devices.size(); ++index) {
            std::cout << "[" << index << "] serial=" << devices[index].serial_number
                      << " ip=" << devices[index].current_ip
                      << " model=" << devices[index].model_name << '\n';
        }

        const std::string serial_number = argc > 2 ? argv[2] : devices.front().serial_number;
        driver.connectBySerial(serial_number);
        driver.setAcquisitionMode(mv3dlp::AcquisitionMode::range_image);
        driver.startAcquisition();

        const mv3dlp::Frame frame = driver.fetchFrame(std::chrono::milliseconds{1000});
        std::cout << "Frame type=" << static_cast<std::uint32_t>(frame.type)
                  << " size=" << frame.width << "x" << frame.height
                  << " bytes=" << frame.data.size()
                  << " frame_no=" << frame.frame_number
                  << " valid=" << (frame.valid ? "true" : "false") << '\n';

        if (frame.type == mv3dlp::FrameType::depth) {
            const auto point_cloud = driver.convertDepthToPointCloud(frame);
            std::cout << "Converted point cloud with " << point_cloud.points.size() << " points." << '\n';
        }

        driver.stopAcquisition();
        driver.disconnect();
        return 0;
    } catch (const mv3dlp::SdkError& error) {
        std::cerr << "SDK error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 3;
    }
}
