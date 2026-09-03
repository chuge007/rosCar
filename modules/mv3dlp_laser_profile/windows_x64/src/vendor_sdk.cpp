#include "vendor_sdk.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mv3dlp::vendor {
namespace {

std::string getEnv(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
}

std::vector<std::string> libraryNames() {
#if defined(_WIN32)
    return {"Mv3dLp.dll"};
#else
    return {"libMv3dLp.so", "Mv3dLp.so"};
#endif
}

std::vector<std::filesystem::path> defaultSearchDirectories() {
    const auto cwd = std::filesystem::current_path();
    std::vector<std::filesystem::path> paths = {
        cwd,
        cwd / "bin",
        cwd / "bin" / "vendor",
        cwd / "runtime",
        cwd / "vendor" / "runtime",
    };

#if defined(_WIN32)
    paths.emplace_back(cwd / "vendor" / "windows-x64" / "bin");
#else
    paths.emplace_back(cwd / "vendor" / "linux-x86_64");
#endif

    return paths;
}

std::vector<std::filesystem::path> resolveCandidates(
    const std::string& explicit_path,
    const std::vector<std::string>& search_paths) {
    std::vector<std::filesystem::path> candidates;
    const auto names = libraryNames();

    auto addPathOrDirectory = [&candidates, &names](const std::string& raw) {
        if (raw.empty()) {
            return;
        }

        const std::filesystem::path path{raw};
        if (path.has_extension()) {
            candidates.push_back(path);
            return;
        }

        for (const auto& name : names) {
            candidates.push_back(path / name);
        }
    };

    addPathOrDirectory(explicit_path);

    for (const auto& path : search_paths) {
        addPathOrDirectory(path);
    }

    addPathOrDirectory(getEnv("MV3DLP_LIBRARY_PATH"));

    const std::string sdk_root = getEnv("MV3DLP_SDK_ROOT");
    if (!sdk_root.empty()) {
        std::filesystem::path root{sdk_root};
#if defined(_WIN32)
        candidates.push_back(root / "Mv3dLp.dll");
        candidates.push_back(root / "Win64_x64" / "Mv3dLp.dll");
#else
        candidates.push_back(root / "libMv3dLp.so");
        candidates.push_back(root / "linux-x86_64" / "libMv3dLp.so");
#endif
    }

    for (const auto& path : defaultSearchDirectories()) {
        addPathOrDirectory(path.string());
    }

    for (const auto& name : names) {
        candidates.emplace_back(name);
    }

    return candidates;
}

void* tryLoadLibrary(const std::filesystem::path& path) {
#if defined(_WIN32)
    return static_cast<void*>(::LoadLibraryA(path.string().c_str()));
#else
    return ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* getSymbolAddress(void* module, const char* symbol_name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(module), symbol_name));
#else
    return ::dlsym(module, symbol_name);
#endif
}

void unloadLibrary(void* module) noexcept {
    if (module == nullptr) {
        return;
    }

#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(module));
#else
    ::dlclose(module);
#endif
}

template <typename T>
T loadSymbol(void* module, const char* symbol_name) {
    void* symbol = getSymbolAddress(module, symbol_name);
    if (symbol == nullptr) {
        throw std::runtime_error(std::string{"Missing SDK symbol: "} + symbol_name);
    }
    return reinterpret_cast<T>(symbol);
}

}  // namespace

VendorSdk::VendorSdk(const std::string& library_path, const std::vector<std::string>& search_paths) {
    const auto candidates = resolveCandidates(library_path, search_paths);

    for (const auto& candidate : candidates) {
        module_ = tryLoadLibrary(candidate);
        if (module_ != nullptr) {
            loaded_library_path_ = candidate.string();
            break;
        }
    }

    if (module_ == nullptr) {
        throw std::runtime_error(
            "Unable to load vendor SDK library. Set DriverOptions.library_path or MV3DLP_LIBRARY_PATH.");
    }

    try {
        get_version_ = loadSymbol<FnGetVersion>(module_, "MV3D_LP_GetVersion");
        initialize_ = loadSymbol<FnInitialize>(module_, "MV3D_LP_Initialize");
        finalize_ = loadSymbol<FnFinalize>(module_, "MV3D_LP_Finalize");
        get_device_number_ = loadSymbol<FnGetDeviceNumber>(module_, "MV3D_LP_GetDeviceNumber");
        get_device_list_ = loadSymbol<FnGetDeviceList>(module_, "MV3D_LP_GetDeviceList");
        open_device_by_ip_ = loadSymbol<FnOpenDeviceByIP>(module_, "MV3D_LP_OpenDeviceByIP");
        open_device_by_sn_ = loadSymbol<FnOpenDeviceBySN>(module_, "MV3D_LP_OpenDeviceBySN");
        close_device_ = loadSymbol<FnCloseDevice>(module_, "MV3D_LP_CloseDevice");
        register_exception_callback_ =
            loadSymbol<FnRegisterExceptionCallBack>(module_, "MV3D_LP_RegisterExceptionCallBack");
        start_measure_ = loadSymbol<FnStartMeasure>(module_, "MV3D_LP_StartMeasure");
        stop_measure_ = loadSymbol<FnStopMeasure>(module_, "MV3D_LP_StopMeasure");
        soft_trigger_ = loadSymbol<FnSoftTrigger>(module_, "MV3D_LP_SoftTrigger");
        get_image_ = loadSymbol<FnGetImage>(module_, "MV3D_LP_GetImage");
        register_image_callback_ =
            loadSymbol<FnRegisterImageDataCallBack>(module_, "MV3D_LP_RegisterImageDataCallBack");
        clear_data_buffer_ = loadSymbol<FnClearDataBuffer>(module_, "MV3D_LP_ClearDataBuffer");
        get_param_ = loadSymbol<FnGetParam>(module_, "MV3D_LP_GetParam");
        set_param_ = loadSymbol<FnSetParam>(module_, "MV3D_LP_SetParam");
        execute_ = loadSymbol<FnExecute>(module_, "MV3D_LP_Execute");
        map_depth_to_point_cloud_ =
            loadSymbol<FnMapDepthToPointCloud>(module_, "MV3D_LP_MapDepthToPointCloud");
    } catch (...) {
        unloadLibrary(module_);
        module_ = nullptr;
        throw;
    }
}

VendorSdk::~VendorSdk() {
    unloadLibrary(module_);
}

VendorSdk::VendorSdk(VendorSdk&& other) noexcept
    : module_(other.module_),
      loaded_library_path_(std::move(other.loaded_library_path_)),
      get_version_(other.get_version_),
      initialize_(other.initialize_),
      finalize_(other.finalize_),
      get_device_number_(other.get_device_number_),
      get_device_list_(other.get_device_list_),
      open_device_by_ip_(other.open_device_by_ip_),
      open_device_by_sn_(other.open_device_by_sn_),
      close_device_(other.close_device_),
      register_exception_callback_(other.register_exception_callback_),
      start_measure_(other.start_measure_),
      stop_measure_(other.stop_measure_),
      soft_trigger_(other.soft_trigger_),
      get_image_(other.get_image_),
      register_image_callback_(other.register_image_callback_),
      clear_data_buffer_(other.clear_data_buffer_),
      get_param_(other.get_param_),
      set_param_(other.set_param_),
      execute_(other.execute_),
      map_depth_to_point_cloud_(other.map_depth_to_point_cloud_) {
    other.module_ = nullptr;
    other.get_version_ = nullptr;
    other.initialize_ = nullptr;
    other.finalize_ = nullptr;
    other.get_device_number_ = nullptr;
    other.get_device_list_ = nullptr;
    other.open_device_by_ip_ = nullptr;
    other.open_device_by_sn_ = nullptr;
    other.close_device_ = nullptr;
    other.register_exception_callback_ = nullptr;
    other.start_measure_ = nullptr;
    other.stop_measure_ = nullptr;
    other.soft_trigger_ = nullptr;
    other.get_image_ = nullptr;
    other.register_image_callback_ = nullptr;
    other.clear_data_buffer_ = nullptr;
    other.get_param_ = nullptr;
    other.set_param_ = nullptr;
    other.execute_ = nullptr;
    other.map_depth_to_point_cloud_ = nullptr;
}

VendorSdk& VendorSdk::operator=(VendorSdk&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    unloadLibrary(module_);

    module_ = other.module_;
    loaded_library_path_ = std::move(other.loaded_library_path_);
    get_version_ = other.get_version_;
    initialize_ = other.initialize_;
    finalize_ = other.finalize_;
    get_device_number_ = other.get_device_number_;
    get_device_list_ = other.get_device_list_;
    open_device_by_ip_ = other.open_device_by_ip_;
    open_device_by_sn_ = other.open_device_by_sn_;
    close_device_ = other.close_device_;
    register_exception_callback_ = other.register_exception_callback_;
    start_measure_ = other.start_measure_;
    stop_measure_ = other.stop_measure_;
    soft_trigger_ = other.soft_trigger_;
    get_image_ = other.get_image_;
    register_image_callback_ = other.register_image_callback_;
    clear_data_buffer_ = other.clear_data_buffer_;
    get_param_ = other.get_param_;
    set_param_ = other.set_param_;
    execute_ = other.execute_;
    map_depth_to_point_cloud_ = other.map_depth_to_point_cloud_;

    other.module_ = nullptr;
    other.get_version_ = nullptr;
    other.initialize_ = nullptr;
    other.finalize_ = nullptr;
    other.get_device_number_ = nullptr;
    other.get_device_list_ = nullptr;
    other.open_device_by_ip_ = nullptr;
    other.open_device_by_sn_ = nullptr;
    other.close_device_ = nullptr;
    other.register_exception_callback_ = nullptr;
    other.start_measure_ = nullptr;
    other.stop_measure_ = nullptr;
    other.soft_trigger_ = nullptr;
    other.get_image_ = nullptr;
    other.register_image_callback_ = nullptr;
    other.clear_data_buffer_ = nullptr;
    other.get_param_ = nullptr;
    other.set_param_ = nullptr;
    other.execute_ = nullptr;
    other.map_depth_to_point_cloud_ = nullptr;

    return *this;
}

const std::string& VendorSdk::loadedLibraryPath() const noexcept {
    return loaded_library_path_;
}

std::string VendorSdk::version() const {
    const char* version_string = get_version_ == nullptr ? nullptr : get_version_();
    return version_string == nullptr ? std::string{} : std::string{version_string};
}

Status VendorSdk::initialize() const {
    return initialize_();
}

Status VendorSdk::finalize() const {
    return finalize_();
}

Status VendorSdk::getDeviceNumber(std::uint32_t* device_number) const {
    return get_device_number_(device_number);
}

Status VendorSdk::getDeviceList(DeviceInfoRaw* devices, std::uint32_t max_count, std::uint32_t* device_count) const {
    return get_device_list_(devices, max_count, device_count);
}

Status VendorSdk::openDeviceByIP(Handle* handle, const char* ip_address) const {
    return open_device_by_ip_(handle, ip_address);
}

Status VendorSdk::openDeviceBySN(Handle* handle, const char* serial_number) const {
    return open_device_by_sn_(handle, serial_number);
}

Status VendorSdk::closeDevice(Handle* handle) const {
    return close_device_(handle);
}

Status VendorSdk::registerExceptionCallBack(Handle handle, ExceptionCallback callback, void* user) const {
    return register_exception_callback_(handle, callback, user);
}

Status VendorSdk::startMeasure(Handle handle) const {
    return start_measure_(handle);
}

Status VendorSdk::stopMeasure(Handle handle) const {
    return stop_measure_(handle);
}

Status VendorSdk::softTrigger(Handle handle) const {
    return soft_trigger_(handle);
}

Status VendorSdk::getImage(Handle handle, ImageDataRaw* image_data, std::uint32_t timeout_ms) const {
    return get_image_(handle, image_data, timeout_ms);
}

Status VendorSdk::registerImageDataCallBack(Handle handle, ImageDataCallback callback, void* user) const {
    return register_image_callback_(handle, callback, user);
}

Status VendorSdk::clearDataBuffer(Handle handle) const {
    return clear_data_buffer_(handle);
}

Status VendorSdk::getParam(Handle handle, const char* key, ParamRaw* param) const {
    return get_param_(handle, key, param);
}

Status VendorSdk::setParam(Handle handle, const char* key, ParamRaw* param) const {
    return set_param_(handle, key, param);
}

Status VendorSdk::execute(Handle handle, const char* key) const {
    return execute_(handle, key);
}

Status VendorSdk::mapDepthToPointCloud(ImageDataRaw* depth_image, ImageDataRaw* point_cloud_image) const {
    return map_depth_to_point_cloud_(depth_image, point_cloud_image);
}

}  // namespace mv3dlp::vendor

