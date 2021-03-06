/* file "SimpleAudioQueriesOnlyWrittenResponse.cpp" */

#include "PhraseSpotterAPI.h"
#include "HoundCloudRequester.h"
#include "HoundConverser.h"
#include "RequestInfoJSON.h"
#include "AcapelaVoiceParametersJSON.h"
#include "ClientCapabilityRegistry.h"
#include "PlatformSpecificClientCapabilities.h"
#include "throwf.h"
#include "Fetch.h"
#include "WAVAudioDecoder.h"
#include "CommandResultJSON.h"
#include "HomeAutomationControlCommandJSON.h"
#include "HomeAutomationBasicControlCommandJSON.h"
#include "HomeAutomationDomainSettingsJSON.h"

#include "SimpleHoundCloudRequesterConfig.h"
#include "OkHoundSink.h"
#include "SimpleRequestInfoPreparer.h"
#include "SimplePartialHandler.h"
#include "SimpleSinks.h"
#include "SimpleDynamicResponseHandlers.h"

extern "C"
{
#include "o_integer.h"
}

#include <iostream>
#include <cstring>
#ifdef RESPEAKERLEDRING
#include <cstdlib>
#include <unistd.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <omp.h>
#include <thread>
#include <atomic>
#include <chrono>
#endif /* RESPEAKERLEDRING */

static void dump_usage(void);
static void process_request(
  HoundConverser * converser,
  ClientCapabilityRegistry::AudioSource * audio_device,
  uint64_t num_of_mic_channels,
  SimplePartialHandler * local_handler);

namespace
{
  class LocalRequestInfoPreparer : public SimpleRequestInfoPreparer
  {
    public:
      LocalRequestInfoPreparer(void)
        :
          SimpleRequestInfoPreparer()
      {
      }

      LocalRequestInfoPreparer(
        const double lat,
        const double lon
      )
        :
          SimpleRequestInfoPreparer(
            lat,
            lon
          )
      {
      }

      LocalRequestInfoPreparer(
        const bool lat_passed,
        const double lat,
        const bool lon_passed,
        const double lon
      )
        :
          SimpleRequestInfoPreparer(
            lat_passed,
            lat,
            lon_passed,
            lon
          )
      {
      }

      ~LocalRequestInfoPreparer(void)
      {
      }

      virtual void prepare(RequestInfoJSON *request_info){
        assert(request_info != NULL);
        SimpleRequestInfoPreparer::prepare(request_info);
#ifdef HOUNDHOUSEDEBUG
        std::cout<<"Beginning RequestInfoJSON preperations by LocalRequestInfoPreparer."<<std::endl;
#endif /* HOUNDHOUSEDEBUG */

        request_info->setResponseAudioShortOrLong(
          RequestInfoJSON::ResponseAudioShortOrLong_Long
        );

        //Need to pick a TTS voice
        request_info->setResponseAudioVoice("Rhona");

        request_info->appendResponseAudioAcceptedEncodings(
          RequestInfoJSON::ResponseAudioAcceptedEncodings_WAV
        );

#ifdef HOUNDHOUSEDEBUG
        std::cout<<"Finished RequestInfoJSON preperations by LocalRequestInfoPreparer."<<std::endl;
#endif /* HOUNDHOUSEDEBUG */
      }
  };

  class LocalResultHandler: public HoundConverser::ResultHandler
  {
    public:
      virtual void
          handle(
        const CommandResultJSON *command_result,
        DynamicResponseHandler *dynamic_response_handler)
      {
#ifdef HOUNDHOUSEDEBUG
        std::cout<<"Beginning result handling by LocalResultHandler."<<std::endl;
#endif /* HOUNDHOUSEDEBUG */

        //Add code to handle specific commands here.

#ifdef HOUNDHOUSEDEBUG
        std::cout<<"Finished result handling by LocalResultHandler."<<std::endl;
#endif /* HOUNDHOUSEDEBUG */
        return;
      }
  };
}

static void dump_usage(const char * const prog_name){
  std::cout << "Usage: "
            << prog_name
            << " "
            << "[--lat LATITUDE]"
            << " "
            << "[--long LONGITUDE]"
            << " "
            << "[--num-of-mic-channels NUMBEROFMICCHANNELS]"
            << " "
#ifdef RESPEAKERLEDRING
            << "[--showstartup]"
            << " "
#endif /* RESPEAKERLEDRING */
            << "ALSARECORDINGDEVICE"
            << " "
            << "ALSAPLAYBACKDEVICE"
            << std::endl;

  std::cout << "ALSARECORDINGDEVICE should be a device name from `arecord -L` prefixed"
            << std::endl
            << "with 'alsa:' ex: alsa:plughw:CARD=PCH,DEV=0"
            << std::endl;

  std::cout << "ALSAPLAYBACKDEVICE should be a device name from `aplay -L` prefixed"
            << std::endl
            << "with 'alsa:' ex: alsa:plughw:CARD=PCH,DEV=0"
            << std::endl;
  return;
}

int main(int argc, char** argv){

#ifdef RESPEAKERLEDRING
  if(argc<3||argc>10){
#else /* RESPEAKERLEDRING */
  if(argc<3||argc>9){
#endif
    dump_usage(*(argv));

    return 1;
  }

  bool passed_lat = false;
  double lat = 0.0;
  bool passed_lon = false;
  double lon = 0.0;
  uint64_t num_of_mic_channels=1ULL;
#ifdef RESPEAKERLEDRING
  bool show_start_up = false;
#endif /* RESPEAKERLEDRING */

  unsigned int positional_arg_counter = 0;
  unsigned int alsa_recording_device_index = 0;
  unsigned int alsa_playback_device_index = 0;

  for(int i=1;i<argc;i++){
    if(std::strcmp("--lat",*(argv+i))==0){
      if(i==argc-1){
        std::cerr << "Error: --lat requires one argument"
                  << std::endl;
        return 1;
      } else {
        char * next_value = *(argv+i+1);
        char * end_ptr = NULL;
        lat = std::strtod(next_value, &end_ptr);
        if(next_value == end_ptr){
          std::cerr << "Error: argument passed to --lat must be a double"
                    << std::endl;
          return 1;
        } else {
          passed_lat = true;
          i++;
        }
      }
    } else if(std::strcmp("--long",*(argv+i))==0) {
      if(i==argc-1){
        std::cerr << "Error: --long requires one argument"
                  << std::endl;
        return 1;
      } else {
        char * next_value = *(argv+i+1);
        char * end_ptr = NULL;
        lon = std::strtod(next_value, &end_ptr);
        if(next_value == end_ptr){
          std::cerr << "Error: argument passed to --long must be a double"
                    << std::endl;
          return 1;
        } else {
          passed_lon = true;
          i++;
        }
      }
    } else if(std::strcmp("--num-of-mic-channels",*(argv+i))==0) {
      if(i==argc-1){
        std::cerr << "Error: --num-of-mic-channels requires one argument"
                  << std::endl;
        return 1;
      } else {
        char * next_value = *(argv+i+1);
        try {
          num_of_mic_channels = std::stoull(next_value);
          i++;
        } catch (...){
          std::cerr << "Error: argument passed to --num-of-mic-channels must be an unsigned long long"
                    << std::endl;
          return 1;
        }
      }
#ifdef RESPEAKERLEDRING
    } else if(std::strcmp("--showstartup",*(argv+i))==0) {
      show_start_up = true;
#endif /* RESPEAKERLEDRING */
    } else {
      if(positional_arg_counter > 1){
        std::cerr << "Error: too many positional arguments passed" <<std::endl;
        return 1;
      }else if(positional_arg_counter == 0){
        if(std::strncmp("alsa:",*(argv+i),5)!=0){
          std::cerr << "ALSARECORDINGDEVICE should be a device name from `arecord -L` prefixed"
                    << std::endl
                    << "with 'alsa:' ex: alsa:plughw:CARD=PCH,DEV=0"
                    << std::endl;
          return 1;
        } else {
          positional_arg_counter++;
          alsa_recording_device_index = i;
        }
      }else if(positional_arg_counter == 1){
          if(std::strncmp("alsa:",*(argv+i),5)!=0){
            std::cerr << "ALSAPLAYBACKDEVICE should be a device name from `aplay -L` prefixed"
                      << std::endl
                      << "with 'alsa:' ex: alsa:plughw:CARD=PCH,DEV=0"
                      << std::endl;
            return 1;
        } else {
          positional_arg_counter++;
          alsa_playback_device_index = i;
        }
      }
    }
  }

  if(positional_arg_counter != 2){
    dump_usage(*(argv));

    return 1;
  }

#ifdef HOUNDHOUSEDEBUG
  std::cout<<"Running with debug messages compiled in."<<std::endl;
#endif /* HOUNDHOUSEDEBUG */

  init_o_integer_module();

  PhraseSpotterSetThreshold(0.4);

#ifdef RESPEAKERLEDRING
  wiringPiSetupGpio();

  pinMode(5,OUTPUT);

  int fd;
  if((fd = wiringPiSPISetup(0, 8000000)) < 0){
    std::cerr << "wiringPiSPISetup failed" << std::endl;
  }

  uint8_t r, g, b, brightness;
  r = 0x00;
  g = 0x00;
  b = 0xFF;
  brightness = 1;
  uint8_t led_frame[48];
  uint8_t close_empty_frame[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  for(int j=0;j<12;j++){
    led_frame[(j*4)] = 0b11100000 | (0b00011111 & brightness);
    led_frame[(j*4)+1] = b;
    led_frame[(j*4)+2] = g;
    led_frame[(j*4)+3] = r;
  }

  brightness = 11;

  led_frame[36] = 0b11100000 | (0b00011111 & brightness);

  brightness = 21;

  led_frame[40] = 0b11100000 | (0b00011111 & brightness);

  brightness = 31;

  led_frame[44] = 0b11100000 | (0b00011111 & brightness);
#endif /* RESPEAKERLEDRING */

  HoundCloudRequester requester(
    simple_config.clientId,
    simple_config.clientKey,
    simple_config.userId
  );

  static SimplePartialHandler local_handler;
  static LocalRequestInfoPreparer local_request_info_preparer(passed_lat,lat,passed_lon,lon);
  static LocalResultHandler local_result_handler;

  HoundConverser converser(&requester);
  converser.register_partial_handler(&local_handler);
  converser.register_request_info_preparer(
    &local_request_info_preparer
  );
  converser.register_supported_feature("HomeAutomationCommands");
  converser.register_result_handler(&local_result_handler);

  ClientCapabilityRegistry capability_registry;
  do_platform_specific_client_capability_registration(&capability_registry);
  capability_registry.register_audio_decoder("WAV", get_wav_audio_decoder());

  try{
    ClientCapabilityRegistry::AudioSource * audio_source_to_use =
      capability_registry.lookup_audio_source(*(argv+alsa_recording_device_index));
    if (audio_source_to_use == NULL) {
      throwf("Specified using audio input `%s', but no audio "
             "input by that name is known.\n", *(argv+alsa_recording_device_index));
    }

    ClientCapabilityRegistry::AudioPlayer * audio_player_to_use =
      capability_registry.lookup_audio_player(*(argv+alsa_playback_device_index));
    if (audio_source_to_use == NULL) {
      throwf("Specified using audio output `%s', but no audio "
             "output by that name is known.\n", *(argv+alsa_playback_device_index));
    }

    static SimpleDynamicResponseHandler
      simple_dynamic_response_handler(
        &local_handler,
        &capability_registry,
        audio_player_to_use
      );

    converser.register_dynamic_response_handler(
      &simple_dynamic_response_handler
    );

#ifdef RESPEAKERLEDRING
    if(show_start_up){
      uint8_t start_up_brightness = 11;
      uint8_t start_up_led_frame[48];
      uint8_t empty_frames[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
      for(int j=0;j<12;j++){
        start_up_led_frame[(j*4)] =
          0b11100000 | (0b00011111 & start_up_brightness);
        start_up_led_frame[(j*4)+1] = 0x00;
        start_up_led_frame[(j*4)+2] = 0xFF;
        start_up_led_frame[(j*4)+3] = 0x00;
      }
      digitalWrite(5,HIGH);
      try {
        if(write(fd,empty_frames,4)<1)
          throw("An error occurred while trying to write to LED buffer");
        if(write(fd,start_up_led_frame,48)<1)
          throw("An error occurred while trying to write to LED buffer");
        if(write(fd,empty_frames,1)<1)
          throw("An error occurred while trying to write to LED buffer");
      } catch (const char * const e){
        std::cerr << e
                  << std::endl;
        return 1;
      }

      std::atomic<bool> should_time_out(true);

      std::thread startup_time_out_thread(
        [](std::atomic<bool> * should_time_out){
          std::this_thread::sleep_for(std::chrono::seconds(5));
          if(*should_time_out == true)
            digitalWrite(5,LOW);
        },
        &should_time_out
      );

      startup_time_out_thread.detach();
      OkHoundSink ok_hound_sink;
      audio_source_to_use->capture(16000, 1, 16, true, &ok_hound_sink);
      should_time_out = false;
    }else{
      OkHoundSink ok_hound_sink;
      audio_source_to_use->capture(16000, 1, 16, true, &ok_hound_sink);
    }
#else /* RESPEAKERLEDRING */
    {
      OkHoundSink ok_hound_sink;
      audio_source_to_use->capture(16000, 1, 16, true, &ok_hound_sink);
    }
#endif

    while(true){
#ifdef RESPEAKERLEDRING
      digitalWrite(5,HIGH);
      #pragma omp parallel sections
      {
        #pragma omp section
        {
          while(digitalRead(5)){
            try {
              if(write(fd,close_empty_frame,4)<1)
                throw("An error occurred while trying to write to LED buffer");
              if(write(fd,led_frame,48)<1)
                throw("An error occurred while trying to write to LED buffer");
              if(write(fd,close_empty_frame,1)<1)
                throw("An error occurred while trying to write to LED buffer");
            } catch (const char * const e){
              std::cerr << e
                        << std::endl;
            }
            for(int i=0;i<11;i++)
              std::swap(led_frame[i*4],led_frame[(i+1)*4]);
            //usleep(100000);
            usleep(75000);
          }
        }
        #pragma omp section
        {
          process_request(&converser, audio_source_to_use, num_of_mic_channels, &local_handler);
        }
      }
#else /* RESPEAKERLEDRING */
      process_request(&converser, audio_source_to_use, num_of_mic_channels, &local_handler);
#endif
      OkHoundSink ok_hound_sink;
      audio_source_to_use->capture(16000, 1, 16, true, &ok_hound_sink);
    }
  }
  catch (char *e1){
    std::cerr<<"Error: "<<e1<<std::endl;
    free(e1);
    return 1;
  }
  catch (const char *e1)
  {
    std::cerr<<"Error: "<<e1<<std::endl;
    return 1;
  }

  cleanup_o_integer_module();
  return 0;
}

static void process_request(
  HoundConverser * converser,
  ClientCapabilityRegistry::AudioSource * audio_device,
  uint64_t num_of_mic_channels,
  SimplePartialHandler * local_handler)
{
  HoundConverser::VoiceRequest *voice_request =
    converser->start_voice_request();

  local_handler->beginQuery();

#ifdef RESPEAKERLEDRING
  SimpleSinkTurnOffLight sink(voice_request, local_handler);
#else /* RESPEAKERLEDRING */
  SimpleSinkWithoutClosingSound sink(voice_request, local_handler);
#endif
  audio_device->capture(16000, num_of_mic_channels, 16, true, &sink);

  voice_request->finish();
  delete voice_request;
}
