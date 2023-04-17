// #include <bits/stdint-uintn.h>

#include <sys/types.h> 

#include <cstdio>  // for printing to stdout
#include <ostream>

#include "Gamma/Analysis.h"
#include "Gamma/Effects.h"
#include "Gamma/Envelope.h"
#include "Gamma/Oscillator.h"

#include "SimpleCompressor/src/GainReductionComputer.h"
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"

#include "SimpleCompressor/src/LookAheadGainReduction.h"
#include "SimpleCompressor/src/LookAheadGainReduction.cpp"
#include "SimpleCompressor/src/GainReductionComputer.h"
#include "SimpleCompressor/src/GainReductionComputer.cpp"

// using namespace gam;
using namespace al;

// This example shows how to use SynthVoice and SynthManagerto create an audio
// visual synthesizer. In a class that inherits from SynthVoice you will
// define the synth's voice parameters and the sound and graphic generation
// processes in the onProcess() functions.

const int BLOCK_SIZE = 128;

float linearToDecibels(float linear) {
  return 20.f * std::log10(std::abs(linear));
}

float decibelsToLinear(float decibels) {
  return std::pow(10.f, decibels / 20.f);
}

template <int block_size>
class CompressorPlugin {
  public:
  bool useLookAhead = false;
  bool debug = true;

  CompressorPlugin() {
    gain.prepare(48000.);
    gain.setThreshold(-5.f);
    gain.setRatio(100.f);
    gain.setKnee(20.f);
    gain.setAttackTime(0.0025f);

    lookahead.setDelayTime(0.005f);
    lookahead.prepare(48000., 2 * block_size);
  };

  AudioIOData& operator()(AudioIOData& io) {
    // TODO get look-ahead working
    io.frame(0);
    for (int i = 0; io() && i < BLOCK_SIZE; i++) {
      sidechain_buf[i] = std::max(std::abs(io.out(0)), std::abs(io.out(1)));
    }

    if (useLookAhead) {
      gain.computeGainInDecibelsFromSidechainSignal(sidechain_buf, gain_buf, block_size);
    } else {
      gain.computeLinearGainFromSidechainSignal(sidechain_buf, gain_buf, block_size);
    }


    if (useLookAhead) {
      lookahead.pushSamples(gain_buf, block_size);
      lookahead.process();
      lookahead.readSamples(look_buf, block_size);

      for (int i = 0; i < block_size; i++) {
        gain_buf[i] = decibelsToLinear(look_buf[i]);
      }
    }

    float pre_peak = 0;
    float duck = 1;
    float post_peak = 0;
    io.frame(0);
    for (int i = 0; io() && i < BLOCK_SIZE; i++) {
      pre_peak = std::max(pre_peak, std::abs(io.out(0)));
      pre_peak = std::max(pre_peak, std::abs(io.out(1)));
      duck = std::min(duck, gain_buf[i]);
      io.out(0) *= gain_buf[i];
      io.out(1) *= gain_buf[i];
      post_peak = std::max(post_peak, std::abs(io.out(0)));
      post_peak = std::max(post_peak, std::abs(io.out(1)));
    }

    if (debug) {
      std::cout << "pre_peak: " << linearToDecibels(pre_peak) << " dB" << std::endl;
      std::cout << "compress: " << linearToDecibels(duck) << " dB" << std::endl;
      std::cout << "post_peak: " << linearToDecibels(post_peak) << " dB" << std::endl;
    }

    return io;
  }

  private:
  GainReductionComputer gain;
  LookAheadGainReduction lookahead;

  float sidechain_buf[block_size];
  float gain_buf[block_size];
  float look_buf[block_size];
};


class SineEnv : public SynthVoice {
 public:
  // Unit generators
  gam::Pan<> mPan;
  gam::Sine<> mOsc;
  gam::Env<3> mAmpEnv;
  // envelope follower to connect audio output to graphics
  gam::EnvFollow<> mEnvFollow;

  // Additional members
  Mesh mMesh;

  // Initialize voice. This function will only be called once per voice when
  // it is created. Voices will be reused if they are idle.
  void init() override {
    // Intialize envelope
    mAmpEnv.curve(0);  // make segments lines
    mAmpEnv.levels(0, 1, 1, 0);
    mAmpEnv.sustainPoint(2);  // Make point 2 sustain until a release is issued

    // We have the mesh be a sphere
    addDisc(mMesh, 1.0, 30);

    // This is a quick way to create parameters for the voice. Trigger
    // parameters are meant to be set only when the voice starts, i.e. they
    // are expected to be constant within a voice instance. (You can actually
    // change them while you are prototyping, but their changes will only be
    // stored and aplied when a note is triggered.)

    createInternalTriggerParameter("amplitude", 0.75, 0.0, 1.0);
    createInternalTriggerParameter("frequency", 60, 20, 5000);
    createInternalTriggerParameter("attackTime", 1.0, 0.00, 0.01);
    createInternalTriggerParameter("releaseTime", 3.0, 0.0, 0.01);
    createInternalTriggerParameter("pan", 0.0, -1.0, 1.0);
  }

  // The audio processing function
  void onProcess(AudioIOData& io) override {
    // Get the values from the parameters and apply them to the corresponding
    // unit generators. You could place these lines in the onTrigger() function,
    // but placing them here allows for realtime prototyping on a running
    // voice, rather than having to trigger a new voice to hear the changes.
    // Parameters will update values once per audio callback because they
    // are outside the sample processing loop.
    mOsc.freq(getInternalParameterValue("frequency"));
    mAmpEnv.lengths()[0] = getInternalParameterValue("attackTime");
    mAmpEnv.lengths()[2] = getInternalParameterValue("releaseTime");
    mPan.pos(getInternalParameterValue("pan"));

    while (io()) {
      float s1 = mOsc() * mAmpEnv() * getInternalParameterValue("amplitude");
      float s2;
      mEnvFollow(s1);
      mPan(s1, s1, s2);

      io.out(0) += s1;
      io.out(1) += s2;
    }

    // We need to let the synth know that this voice is done
    // by calling the free(). This takes the voice out of the
    // rendering chain
    if (mAmpEnv.done() && (mEnvFollow.value() < 0.001f)) free();
  }

  // The graphics processing function
  void onProcess(Graphics& g) override {
    // Get the paramter values on every video frame, to apply changes to the
    // current instance
    float frequency = getInternalParameterValue("frequency");
    float amplitude = getInternalParameterValue("amplitude");
    // Now draw
    g.pushMatrix();
    g.translate(frequency / 200 - 3, amplitude, -8);
    g.scale(1 - amplitude, amplitude, 1);
    //g.color(mEnvFollow.value(), frequency / 1000, mEnvFollow.value() * 10, 0.4);
    g.color(1, 0, 1, 1);
    g.draw(mMesh);
    g.popMatrix();
  }

  // The triggering functions just need to tell the envelope to start or release
  // The audio processing function checks when the envelope is done to remove
  // the voice from the processing chain.
  void onTriggerOn() override { mAmpEnv.reset(); }

  void onTriggerOff() override { mAmpEnv.release(); }
};

// We make an app.
class MyApp : public App, public MIDIMessageHandler {
 public:
  // GUI manager for SineEnv voices
  // The name provided determines the name of the directory
  // where the presets and sequences are stored
  SynthGUIManager<SineEnv> synthManager{"SineEnv"};

  CompressorPlugin<BLOCK_SIZE> compressor;

  RtMidiIn midiIn;

  void onInit() override {
      // Check for connected MIDI devices
      if (midiIn.getPortCount() > 0) {
          // Bind ourself to the RtMidiIn object, to have the onMidiMessage()
          // callback called whenever a MIDI message is received
          MIDIMessageHandler::bindTo(midiIn);

          // Open the last device found
          unsigned int port = midiIn.getPortCount() - 1;
          midiIn.openPort(port);
          printf("Opened port to %s\n", midiIn.getPortName(port).c_str());
      }
      else {
          printf("Error: No MIDI devices found.\n");
      }
  }

  // This gets called whenever a MIDI message is received on the port
  void onMIDIMessage(const MIDIMessage& m) {
      printf("%s: ", MIDIByte::messageTypeString(m.status()));

      // Here we demonstrate how to parse common channel messages
      switch (m.type()) {
      case MIDIByte::NOTE_ON:
	  if (m.velocity() > 0.) {
		  synthManager.voice()->setInternalParameterValue(
		      "frequency", ::pow(2.f, (m.noteNumber() - 69.f) / 12.f) * 440.f);
		  synthManager.triggerOn(m.noteNumber());
	  } else {
              synthManager.triggerOff(m.noteNumber());
	  }
          break;

      case MIDIByte::NOTE_OFF:
          if (m.noteNumber() > 0) {
              synthManager.triggerOff(m.noteNumber());
          }
          break;

      case MIDIByte::PITCH_BEND:
          printf("Value %f", m.pitchBend());
          break;

          // Control messages need to be parsed again...
      case MIDIByte::CONTROL_CHANGE:
          printf("%s ", MIDIByte::controlNumberString(m.controlNumber()));
          switch (m.controlNumber()) {
          case MIDIByte::MODULATION:
              printf("%f", m.controlValue());
              break;

          case MIDIByte::EXPRESSION:
              printf("%f", m.controlValue());
              break;
          }
          break;
      default:;
      }

      // If it's a channel message, print out channel number
      if (m.isChannelMessage()) {
          printf(" (MIDI chan %u)", m.channel() + 1);
      }

      printf("\n");

      // Print the raw byte values and time stamp
      printf("\tBytes = ");
      for (unsigned i = 0; i < 3; ++i) {
          printf("%3u ", (int)m.bytes[i]);
      }
      printf(", time = %g\n", m.timeStamp());
  }

  // This function is called right after the window is created
  // It provides a grphics context to initialize ParameterGUI
  // It's also a good place to put things that should
  // happen once at startup.
  void onCreate() override {
    navControl().active(false);  // Disable navigation via keyboard, since we
                                 // will be using keyboard for note triggering

    // Set sampling rate for Gamma objects from app's audio
    gam::sampleRate(audioIO().framesPerSecond());

    imguiInit();

    // Play example sequence. Comment this line to start from scratch
    //synthManager.synthSequencer().playSequence("synth1.synthSequence");
    synthManager.synthRecorder().verbose(true);
  }

  // The audio callback function. Called when audio hardware requires data
  void onSound(AudioIOData& io) override {
    synthManager.render(io);  // Render audio
    compressor(io);
  }

  void onAnimate(double dt) override {
    // The GUI is prepared here
    imguiBeginFrame();
    // Draw a window that contains the synth control panel
    synthManager.drawSynthControlPanel();
    imguiEndFrame();
  }

  // The graphics callback function.
  void onDraw(Graphics& g) override {
    g.clear();
    // Render the synth's graphics
    synthManager.render(g);

    // GUI is drawn here
    imguiDraw();
  }

  // Whenever a key is pressed, this function is called
  bool onKeyDown(Keyboard const& k) override {
    if (ParameterGUI::usingKeyboard()) {  // Ignore keys if GUI is using
                                          // keyboard
      return true;
    }
    if (k.shift()) {
      // If shift pressed then keyboard sets preset
      int presetNumber = asciiToIndex(k.key());
      synthManager.recallPreset(presetNumber);
    } else {
      // Otherwise trigger note for polyphonic synth
      int midiNote = asciiToMIDI(k.key());
      if (midiNote > 0) {
        synthManager.voice()->setInternalParameterValue(
            "frequency", ::pow(2.f, (midiNote - 69.f) / 12.f) * 432.f);
        synthManager.triggerOn(midiNote);
      }
    }
    return true;
  }

  // Whenever a key is released this function is called
  bool onKeyUp(Keyboard const& k) override {
    int midiNote = asciiToMIDI(k.key());
    if (midiNote > 0) {
      synthManager.triggerOff(midiNote);
    }
    return true;
  }

  void onExit() override { imguiShutdown(); }
};

int main() {
  // Create app instance
  MyApp app;

  // Set up audio
  app.configureAudio(48000., BLOCK_SIZE, 2, 0);

  app.start();
  return 0;
}
