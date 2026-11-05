// SPDX-License-Identifier: MIT
//
// PICam backend. Written against the PICam 5.x C API (see the PICam
// Programmer's Manual, Teledyne Princeton Instruments). Every SDK call is
// checked; failures become CameraError with the SDK's own error string.
#include "superlotis/picam_backend.hpp"

#include <picam.h>

#include <cmath>
#include <cstring>
#include <string>

#include "superlotis/log.hpp"

namespace superlotis {

namespace {

std::string enum_string(PicamEnumeratedType type, piint value) {
    const pichar* s = nullptr;
    if (Picam_GetEnumerationString(type, value, &s) != PicamError_None || s == nullptr)
        return "code " + std::to_string(value);
    std::string out(s);
    Picam_DestroyString(s);
    return out;
}

piint to_picam(ShutterMode mode) {
    switch (mode) {
        case ShutterMode::Normal: return PicamShutterTimingMode_Normal;
        case ShutterMode::AlwaysClosed: return PicamShutterTimingMode_AlwaysClosed;
        case ShutterMode::AlwaysOpen: return PicamShutterTimingMode_AlwaysOpen;
    }
    throw CameraError("invalid shutter mode");
}

piint to_picam(AdcGain gain) {
    switch (gain) {
        case AdcGain::Low: return PicamAdcAnalogGain_Low;
        case AdcGain::Medium: return PicamAdcAnalogGain_Medium;
        case AdcGain::High: return PicamAdcAnalogGain_High;
    }
    throw CameraError("invalid ADC gain");
}

void check(PicamError err, const char* what) {
    if (err != PicamError_None)
        throw CameraError(std::string("PICam: ") + what + " failed (" +
                          enum_string(PicamEnumeratedType_Error, err) + ")");
}

}  // namespace

struct PicamBackend::Impl {
    PicamConfig config;
    PicamHandle camera = nullptr;
    PicamCameraID id{};
    bool library_initialized = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    void commit() {
        pibln committed = false;
        check(Picam_AreParametersCommitted(camera, &committed), "AreParametersCommitted");
        if (committed) return;
        const PicamParameter* failed = nullptr;
        piint failed_count = 0;
        check(Picam_CommitParameters(camera, &failed, &failed_count), "CommitParameters");
        if (failed_count > 0) {
            std::string names;
            for (piint i = 0; i < failed_count; ++i) {
                if (i) names += ", ";
                names += std::to_string(static_cast<int>(failed[i]));
            }
            Picam_DestroyParameters(failed);
            throw CameraError("PICam: camera rejected parameter id(s): " + names);
        }
    }

    void read_geometry() {
        const PicamRois* rois = nullptr;
        check(Picam_GetParameterRoisValue(camera, PicamParameter_Rois, &rois), "GetParameterRoisValue");
        if (rois == nullptr || rois->roi_count != 1) {
            if (rois) Picam_DestroyRois(rois);
            throw CameraError("PICam: expected exactly one region of interest");
        }
        const PicamRoi& r = rois->roi_array[0];
        width = static_cast<std::uint32_t>(r.width / r.x_binning);
        height = static_cast<std::uint32_t>(r.height / r.y_binning);
        Picam_DestroyRois(rois);
    }
};

PicamBackend::PicamBackend(PicamConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    check(Picam_InitializeLibrary(), "InitializeLibrary");
    impl_->library_initialized = true;

    if (config.demo) {
        log::warn("PICam demo camera requested: frames will NOT come from real hardware");
        check(Picam_ConnectDemoCamera(PicamModel_Sophia2048BUV135, "XO30000923", &impl_->id),
              "ConnectDemoCamera");
        check(Picam_OpenCamera(&impl_->id, &impl_->camera), "OpenCamera(demo)");
    } else {
        const PicamError err = Picam_OpenFirstCamera(&impl_->camera);
        if (err != PicamError_None) {
            Picam_UninitializeLibrary();
            impl_->library_initialized = false;
            check(err, "OpenFirstCamera (is the camera powered and connected?)");
        }
        check(Picam_GetCameraID(impl_->camera, &impl_->id), "GetCameraID");
    }

    // One readout per acquisition; geometry comes from the active ROI.
    check(Picam_SetParameterLargeIntegerValue(impl_->camera, PicamParameter_ReadoutCount, 1),
          "Set ReadoutCount");
    impl_->commit();
    impl_->read_geometry();

    const CameraInfo i = info();
    log::info("Opened ", i.model, " SN ", i.serial, " (", i.width, "x", i.height, ")");
}

PicamBackend::~PicamBackend() {
    if (impl_->camera) Picam_CloseCamera(impl_->camera);
    if (impl_->config.demo) Picam_DisconnectDemoCamera(&impl_->id);
    if (impl_->library_initialized) Picam_UninitializeLibrary();
}

CameraInfo PicamBackend::info() const {
    CameraInfo out;
    out.model = enum_string(PicamEnumeratedType_Model, impl_->id.model);
    out.serial = impl_->id.serial_number;
    out.sensor = impl_->id.sensor_name;
    out.width = impl_->width;
    out.height = impl_->height;
    out.simulated = impl_->config.demo;
    return out;
}

void PicamBackend::set_adc_gain(AdcGain gain) {
    check(Picam_SetParameterIntegerValue(impl_->camera, PicamParameter_AdcAnalogGain, to_picam(gain)),
          "Set AdcAnalogGain");
    impl_->commit();
}

AdcGain PicamBackend::adc_gain() {
    piint value = 0;
    check(Picam_GetParameterIntegerValue(impl_->camera, PicamParameter_AdcAnalogGain, &value),
          "Get AdcAnalogGain");
    switch (value) {
        case PicamAdcAnalogGain_Low: return AdcGain::Low;
        case PicamAdcAnalogGain_Medium: return AdcGain::Medium;
        case PicamAdcAnalogGain_High: return AdcGain::High;
        default: throw CameraError("PICam: unexpected AdcAnalogGain value " + std::to_string(value));
    }
}

void PicamBackend::set_temperature_setpoint(double celsius) {
    // The set point can be changed at any time; the lock status only reports
    // whether the sensor has reached it.
    check(Picam_SetParameterFloatingPointValue(impl_->camera, PicamParameter_SensorTemperatureSetPoint,
                                               celsius),
          "Set SensorTemperatureSetPoint");
    impl_->commit();
}

TemperatureStatus PicamBackend::temperature() {
    TemperatureStatus t;
    piflt reading = 0.0;
    piflt setpoint = 0.0;
    piint status = 0;
    check(Picam_ReadParameterFloatingPointValue(impl_->camera, PicamParameter_SensorTemperatureReading,
                                                &reading),
          "Read SensorTemperatureReading");
    check(Picam_GetParameterFloatingPointValue(impl_->camera, PicamParameter_SensorTemperatureSetPoint,
                                               &setpoint),
          "Get SensorTemperatureSetPoint");
    check(Picam_ReadParameterIntegerValue(impl_->camera, PicamParameter_SensorTemperatureStatus, &status),
          "Read SensorTemperatureStatus");
    t.reading_c = reading;
    t.setpoint_c = setpoint;
    t.locked = (status == PicamSensorTemperatureStatus_Locked);
    return t;
}

Frame PicamBackend::acquire(double exposure_s, ShutterMode shutter) {
    // PICam expresses exposure time in milliseconds.
    check(
        Picam_SetParameterFloatingPointValue(impl_->camera, PicamParameter_ExposureTime, exposure_s * 1000.0),
        "Set ExposureTime");
    check(Picam_SetParameterIntegerValue(impl_->camera, PicamParameter_ShutterTimingMode, to_picam(shutter)),
          "Set ShutterTimingMode");
    impl_->commit();

    piint stride = 0;
    check(Picam_GetParameterIntegerValue(impl_->camera, PicamParameter_ReadoutStride, &stride),
          "Get ReadoutStride");

    const double timeout_ms = (exposure_s + impl_->config.readout_timeout_s) * 1000.0;
    PicamAvailableData data{};
    PicamAcquisitionErrorsMask errors = PicamAcquisitionErrorsMask_None;
    check(Picam_Acquire(impl_->camera, 1, static_cast<piint>(std::ceil(timeout_ms)), &data, &errors),
          "Acquire");
    if (errors != PicamAcquisitionErrorsMask_None)
        throw CameraError("PICam: acquisition reported errors (mask " +
                          std::to_string(static_cast<int>(errors)) + ")");
    if (data.readout_count < 1 || data.initial_readout == nullptr)
        throw CameraError("PICam: acquisition returned no data");

    Frame frame;
    frame.width = impl_->width;
    frame.height = impl_->height;
    const std::size_t n = static_cast<std::size_t>(frame.width) * frame.height;
    if (static_cast<std::size_t>(stride) < n * sizeof(std::uint16_t))
        throw CameraError("PICam: readout smaller than the configured ROI");
    frame.pixels.resize(n);
    std::memcpy(frame.pixels.data(), data.initial_readout, n * sizeof(std::uint16_t));
    return frame;
}

}  // namespace superlotis
