/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2016 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_ANDROID

/* Output audio to Android */

#include "SDL_assert.h"
#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_androidaudio.h"

#include "../../core/android/SDL_android.h"

#include <android/log.h>

static SDL_AudioDevice* audioDevice = NULL;
static SDL_AudioDevice* captureDevice = NULL;

static int
AndroidAUD_OpenDevice(_THIS, void *handle, const char *devname, int iscapture)
{
    SDL_AudioFormat test_format;

    SDL_assert((captureDevice == NULL) || !iscapture);
    SDL_assert((audioDevice == NULL) || iscapture);

    if (iscapture) {
        captureDevice = this;
    } else {
        audioDevice = this;
    }

    this->hidden = (struct SDL_PrivateAudioData *) SDL_calloc(1, (sizeof *this->hidden));
    if (this->hidden == NULL) {
        return SDL_OutOfMemory();
    }

    test_format = SDL_FirstAudioFormat(this->spec.format);
    while (test_format != 0) { /* no "UNKNOWN" constant */
        if ((test_format == AUDIO_U8) || (test_format == AUDIO_S16LSB)) {
            this->spec.format = test_format;
            break;
        }
        test_format = SDL_NextAudioFormat();
    }

    if (test_format == 0) {
        /* Didn't find a compatible format :( */
        return SDL_SetError("No compatible audio format!");
    }

    if (this->spec.channels > 1) {
        this->spec.channels = 2;
    } else {
        this->spec.channels = 1;
    }

    if (this->spec.freq < 8000) {
        this->spec.freq = 8000;
    }
    if (this->spec.freq > 48000) {
        this->spec.freq = 48000;
    }

    /* TODO: pass in/return a (Java) device ID */
    this->spec.samples = Android_JNI_OpenAudioDevice(iscapture, this->spec.freq, this->spec.format == AUDIO_U8 ? 0 : 1, this->spec.channels, this->spec.samples);

    if (this->spec.samples == 0) {
        /* Init failed? */
        return SDL_SetError("Java-side initialization failed!");
    }

    SDL_CalculateAudioSpec(&this->spec);

    return 0;
}

static void
AndroidAUD_PlayDevice(_THIS)
{
    Android_JNI_WriteAudioBuffer();
}

static Uint8 *
AndroidAUD_GetDeviceBuf(_THIS)
{
    return Android_JNI_GetAudioBuffer();
}

static int
AndroidAUD_CaptureFromDevice(_THIS, void *buffer, int buflen)
{
    return Android_JNI_CaptureAudioBuffer(buffer, buflen);
}

static void
AndroidAUD_FlushCapture(_THIS)
{
    Android_JNI_FlushCapturedAudio();
}

static void
AndroidAUD_CloseDevice(_THIS)
{
    /* At this point SDL_CloseAudioDevice via close_audio_device took care of terminating the audio thread
       so it's safe to terminate the Java side buffer and AudioTrack
     */
    Android_JNI_CloseAudioDevice(this->iscapture);
    if (this->iscapture) {
        SDL_assert(captureDevice == this);
        captureDevice = NULL;
    } else {
        SDL_assert(audioDevice == this);
        audioDevice = NULL;
    }
    SDL_free(this->hidden);
}

static int
AndroidAUD_Init(SDL_AudioDriverImpl * impl)
{
    /* Set the function pointers */
    impl->OpenDevice = AndroidAUD_OpenDevice;
    impl->PlayDevice = AndroidAUD_PlayDevice;
    impl->GetDeviceBuf = AndroidAUD_GetDeviceBuf;
    impl->CloseDevice = AndroidAUD_CloseDevice;
    impl->CaptureFromDevice = AndroidAUD_CaptureFromDevice;
    impl->FlushCapture = AndroidAUD_FlushCapture;

    /* and the capabilities */
    impl->HasCaptureSupport = SDL_TRUE;
    impl->OnlyHasDefaultOutputDevice = 1;
    impl->OnlyHasDefaultCaptureDevice = 1;

    return 1;   /* this audio target is available. */
}

AudioBootStrap ANDROIDAUD_bootstrap = {
    "android", "SDL Android audio driver", AndroidAUD_Init, 0
};

/* Pause (block) all non already paused audio devices by taking their mixer lock */
void AndroidAUD_PauseDevices(void)
{
    /* TODO: Handle multiple devices? */
    struct SDL_PrivateAudioData *private;
    if(audioDevice != NULL && audioDevice->hidden != NULL) {
        private = (struct SDL_PrivateAudioData *) audioDevice->hidden;
        if (SDL_AtomicGet(&audioDevice->paused)) {
            /* The device is already paused, leave it alone */
            private->resume = SDL_FALSE;
        }
        else {
            SDL_LockMutex(audioDevice->mixer_lock);
            SDL_AtomicSet(&audioDevice->paused, 1);
            private->resume = SDL_TRUE;
        }
    }

    if(captureDevice != NULL && captureDevice->hidden != NULL) {
        private = (struct SDL_PrivateAudioData *) captureDevice->hidden;
        if (SDL_AtomicGet(&captureDevice->paused)) {
            /* The device is already paused, leave it alone */
            private->resume = SDL_FALSE;
        }
        else {
            SDL_LockMutex(captureDevice->mixer_lock);
            SDL_AtomicSet(&captureDevice->paused, 1);
            private->resume = SDL_TRUE;
        }
    }
}

/* Resume (unblock) all non already paused audio devices by releasing their mixer lock */
void AndroidAUD_ResumeDevices(void)
{
    /* TODO: Handle multiple devices? */
    struct SDL_PrivateAudioData *private;
    if(audioDevice != NULL && audioDevice->hidden != NULL) {
        private = (struct SDL_PrivateAudioData *) audioDevice->hidden;
        if (private->resume) {
            SDL_AtomicSet(&audioDevice->paused, 0);
            private->resume = SDL_FALSE;
            SDL_UnlockMutex(audioDevice->mixer_lock);
        }
    }

    if(captureDevice != NULL && captureDevice->hidden != NULL) {
        private = (struct SDL_PrivateAudioData *) captureDevice->hidden;
        if (private->resume) {
            SDL_AtomicSet(&captureDevice->paused, 0);
            private->resume = SDL_FALSE;
            SDL_UnlockMutex(captureDevice->mixer_lock);
        }
    }
}


#endif /* SDL_AUDIO_DRIVER_ANDROID */

/* vi: set ts=4 sw=4 expandtab: */

