# mv3dlp_laser_profile Windows x64

This is the Windows x64 delivery package.

## Contents

- `include/`: public C++ headers
- `src/`: wrapper implementation
- `examples/fetch_frame.cpp`: minimal usage example
- `bin/`: packaged vendor runtime DLLs, config files, and runtime dependencies
- `vendor/lib/Mv3dLp.lib`: vendor import library

## Usage

```cpp
#include "mv3dlp_laser_profile/driver.hpp"

mv3dlp::Driver driver;
auto devices = driver.enumerateDevices();
driver.connectBySerial(devices.front().serial_number);
driver.setAcquisitionMode(mv3dlp::AcquisitionMode::range_image);
driver.startAcquisition();
auto frame = driver.fetchFrame(std::chrono::milliseconds{1000});
driver.stopAcquisition();
driver.disconnect();
```

## Notes

- The wrapper loads `bin/Mv3dLp.dll` at runtime.
- If you move the DLLs elsewhere, set `DriverOptions.library_path`.

## Package status

- `Mv3dLp.lib`: present
- `Mv3dLpSDK runtime`: present
- `MV3D runtime`: present