/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LightsService"

#include "jni.h"
#include <nativehelper/JNIHelp.h>
#include "android_runtime/AndroidRuntime.h"

#include <android/hardware/light/2.0/ILight.h>
#include <android/hardware/light/2.0/types.h>
#include <vendor/samsung/hardware/light/2.0/ISecLight.h>
#include <vendor/samsung/hardware/light/2.0/types.h>
#include <android-base/chrono_utils.h>
#include <utils/misc.h>
#include <utils/Log.h>
#include <map>
#include <stdio.h>

namespace android {

using Brightness = ::android::hardware::light::V2_0::Brightness;
using Flash      = ::android::hardware::light::V2_0::Flash;
using ILight     = ::android::hardware::light::V2_0::ILight;
using LightState = ::android::hardware::light::V2_0::LightState;
using Status     = ::android::hardware::light::V2_0::Status;
using Type       = ::android::hardware::light::V2_0::Type;
template<typename T>
using Return     = ::android::hardware::Return<T>;

using ISecLight  = ::vendor::samsung::hardware::light::V2_0::ISecLight;
using SecType    = ::vendor::samsung::hardware::light::V2_0::SecType;

class LightHal {
private:
    static sp<ILight> sLight;
    static sp<ISecLight> sSecLight;
    static bool sLightInit;

    LightHal() {}

public:
    static void disassociate() {
        sLightInit = false;
        sLight = nullptr;
    }

    static sp<ILight> associate() {
        if ((sLight == nullptr && !sLightInit) ||
                (sLight != nullptr && !sLight->ping().isOk())) {
            // will return the hal if it exists the first time.
            sLight = ILight::getService();
            sSecLight = ISecLight::getService();
            sLightInit = true;

            if (sLight == nullptr) {
                ALOGE("Unable to get ILight interface.");
            }
        }

        return sLight;
    }

    static sp<ISecLight> getSecLight() {
        return sSecLight;
    }
};

sp<ILight> LightHal::sLight = nullptr;
sp<ISecLight> LightHal::sSecLight = nullptr;
bool LightHal::sLightInit = false;

static bool validate(jint light, jint flash, jint brightness) {
    bool valid = true;

    if (light < 0 || light >= static_cast<jint>(Type::COUNT)) {
        ALOGE("Invalid light parameter %d.", light);
        valid = false;
    }

    if (flash != static_cast<jint>(Flash::NONE) &&
        flash != static_cast<jint>(Flash::TIMED) &&
        flash != static_cast<jint>(Flash::HARDWARE)) {
        ALOGE("Invalid flash parameter %d.", flash);
        valid = false;
    }

    if (brightness != static_cast<jint>(Brightness::USER) &&
        brightness != static_cast<jint>(Brightness::SENSOR) &&
        brightness != static_cast<jint>(Brightness::LOW_PERSISTENCE)) {
        ALOGE("Invalid brightness parameter %d.", brightness);
        valid = false;
    }

    if (brightness == static_cast<jint>(Brightness::LOW_PERSISTENCE) &&
        light != static_cast<jint>(Type::BACKLIGHT)) {
        ALOGE("Cannot set low-persistence mode for non-backlight device.");
        valid = false;
    }

    return valid;
}

static LightState constructState(
        jint colorARGB,
        jint flashMode,
        jint onMS,
        jint offMS,
        jint brightnessMode){
    Flash flash = static_cast<Flash>(flashMode);
    Brightness brightness = static_cast<Brightness>(brightnessMode);

    LightState state{};

    if (brightness == Brightness::LOW_PERSISTENCE) {
        state.flashMode = Flash::NONE;
    } else {
        // Only set non-brightness settings when not in low-persistence mode
        state.flashMode = flash;
        state.flashOnMs = onMS;
        state.flashOffMs = offMS;
    }

    state.color = colorARGB;
    state.brightnessMode = brightness;

    return state;
}

static void processReturn(
        const Return<Status> &ret,
        Type type,
        const LightState &state) {
    if (!ret.isOk()) {
        ALOGE("Failed to issue set light command.");
        LightHal::disassociate();
        return;
    }

    switch (static_cast<Status>(ret)) {
        case Status::SUCCESS:
            break;
        case Status::LIGHT_NOT_SUPPORTED:
            ALOGE("Light requested not available on this device. %d", type);
            break;
        case Status::BRIGHTNESS_NOT_SUPPORTED:
            ALOGE("Brightness parameter not supported on this device: %d",
                state.brightnessMode);
            break;
        case Status::UNKNOWN:
        default:
            ALOGE("Unknown error setting light.");
    }
}

static void setLight_native(
        JNIEnv* /* env */,
        jobject /* clazz */,
        jint light,
        jint colorARGB,
        jint flashMode,
        jint onMS,
        jint offMS,
        jint brightnessMode) {

    if (!validate(light, flashMode, brightnessMode)) {
        return;
    }

    sp<ILight> hal = LightHal::associate();

    if (hal == nullptr) {
        return;
    }

    sp<ISecLight> secHal = LightHal::getSecLight();

    if(secHal != nullptr) {
        SecType type = static_cast<SecType>(light);
        LightState state = constructState(
                colorARGB, flashMode, onMS, offMS, brightnessMode);

        {
            android::base::Timer t;
            Return<Status> ret = secHal->setLightSec(type, state);
            processReturn(ret, static_cast<Type>(light), state);
            if (t.duration() > 50ms) ALOGD("Excessive delay setting light");
        }
    }

    Type type = static_cast<Type>(light);
    LightState state = constructState(
        colorARGB, flashMode, onMS, offMS, brightnessMode);

    {
        android::base::Timer t;
        Return<Status> ret = hal->setLight(type, state);
        processReturn(ret, type, state);
        if (t.duration() > 50ms) ALOGD("Excessive delay setting light");
    }
}

static const JNINativeMethod method_table[] = {
    { "setLight_native", "(IIIIII)V", (void*)setLight_native },
};

int register_android_server_LightsService(JNIEnv *env) {
    return jniRegisterNativeMethods(env, "com/android/server/lights/LightsService",
            method_table, NELEM(method_table));
}

};
