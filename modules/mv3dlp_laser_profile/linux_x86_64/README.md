# mv3dlp_laser_profile Linux x86_64

This is the Linux x86_64 delivery package.

## Contents

- `include/`: public C++ headers
- `src/`: wrapper implementation
- `examples/fetch_frame.cpp`: minimal usage example
- `vendor/linux-x86_64/`: drop the official vendor `.so` files here

## Usage

```cpp
#include "mv3dlp_laser_profile/driver.hpp"

mv3dlp::DriverOptions options;
options.library_path = "./vendor/linux-x86_64/libMv3dLp.so";

mv3dlp::Driver driver(options);
auto devices = driver.enumerateDevices();
driver.connectBySerial(devices.front().serial_number);
driver.setAcquisitionMode(mv3dlp::AcquisitionMode::range_image);
driver.startAcquisition();
auto frame = driver.fetchFrame(std::chrono::milliseconds{1000});
driver.stopAcquisition();
driver.disconnect();
```

## Current status

- Linux wrapper code is ready.
- No vendor Linux `.so` was present in the local SDK installation checked on 2026-08-13.
- Add the official Linux runtime under `vendor/linux-x86_64/` before deployment.

## Notes

- The C++ API is kept aligned with the Windows package.
- For ROS integration later, keep ROS2 code in a separate package that depends on this wrapper.