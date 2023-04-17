#include <cstdio>  // for printing to stdout

#include "Gamma/Analysis.h"
#include "Gamma/Effects.h"
#include "Gamma/Envelope.h"
#include "Gamma/Gamma.h"
#include "Gamma/Oscillator.h"
#include "Gamma/Types.h"
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"

using namespace gam;
using namespace al;
using namespace std;

std::string midiToNote(int midi) {
    switch(midi){
        case 60:
        case 72:
        case 84:
            return "C";
        case 61:
        case 73:
        case 85:
            return "C#";
        case 62:
        case 74:
        case 86:
            return "D";
        case 63:
        case 75:
        case 87:
            return "D#";
        case 64:
        case 76:
        case 88:
            return "E";
        case 65:
        case 77:
            return "F";
        case 66:
        case 78:
            return "F#";
        case 67:
        case 79:
            return "G";
        case 68:
        case 80:
            return "G#";
        case 69:
        case 81:
            return "A";
        case 70:
        case 82:
            return "A#";
        case 71:
        case 83:
            return "B";
    }
}
class Sub : public SynthVoice {
public:

    // Unit generators
    float mNoiseMix;
    gam::Pan<> mPan;
    gam::ADSR<> mAmpEnv;
    gam::EnvFollow<> mEnvFollow;  // envelope follower to connect audio output to graphics
    gam::DSF<> mOsc;
    gam::NoiseWhite<> mNoise;
    gam::Reson<> mRes;
    gam::Env<2> mCFEnv;
    gam::Env<2> mBWEnv;
    // Additional members
    Mesh mMesh;
    Mesh mesh2;

    int frame = 0;
    int f = 0;

    // Initialize voice. This function will nly be called once per voice
    void init() override {
        mAmpEnv.curve(0); // linear segments
        mAmpEnv.levels(0,1.0,1.0,0); // These tables are not normalized, so scale to 0.3
        mAmpEnv.sustainPoint(2); // Make point 2 sustain until a release is issued
        mCFEnv.curve(0);
        mBWEnv.curve(0);
        mOsc.harmonics(12);
        // We have the mesh be a sphere
        //addDisc(mMesh, 1.0, 30);
        //addCone(mMesh, 1.0, );
        addRect(mMesh, 0, 0, 1, 1);
        addDisc(mesh2, 1.0, 30);

        createInternalTriggerParameter("amplitude", 0.3, 0.0, 1.0);
        createInternalTriggerParameter("frequency", 60, 20, 5000);
        createInternalTriggerParameter("attackTime", 0.1, 0.01, 3.0);
        createInternalTriggerParameter("releaseTime", 3.0, 0.1, 10.0);
        createInternalTriggerParameter("sustain", 0.7, 0.0, 1.0);
        createInternalTriggerParameter("curve", 4.0, -10.0, 10.0);
        createInternalTriggerParameter("noise", 0.0, 0.0, 1.0);
        createInternalTriggerParameter("envDur",1, 0.0, 5.0);
        createInternalTriggerParameter("cf1", 400.0, 10.0, 5000);
        createInternalTriggerParameter("cf2", 400.0, 10.0, 5000);
        createInternalTriggerParameter("cfRise", 0.5, 0.1, 2);
        createInternalTriggerParameter("bw1", 700.0, 10.0, 5000);
        createInternalTriggerParameter("bw2", 900.0, 10.0, 5000);
        createInternalTriggerParameter("bwRise", 0.5, 0.1, 2);
        createInternalTriggerParameter("hmnum", 12.0, 5.0, 20.0);
        createInternalTriggerParameter("hmamp", 1.0, 0.0, 1.0);
        createInternalTriggerParameter("pan", 0.0, -1.0, 1.0);

    }

    //
    
    virtual void onProcess(AudioIOData& io) override {
        updateFromParameters();
        float amp = getInternalParameterValue("amplitude");
        float noiseMix = getInternalParameterValue("noise");
        while(io()){
            // mix oscillator with noise
            float s1 = mOsc()*(1-noiseMix) + mNoise()*noiseMix;

            // apply resonant filter
            mRes.set(mCFEnv(), mBWEnv());
            s1 = mRes(s1);

            // appy amplitude envelope
            s1 *= mAmpEnv() * amp;

            float s2;
            mPan(s1, s1,s2);
            io.out(0) += s1;
            io.out(1) += s2;
        }
        
        
        if(mAmpEnv.done() && (mEnvFollow.value() < 0.001f)) free();
    }
    float map(float value,  float istart, float istop, float ostart, float ostop){
        return ostart + (ostop - ostart) * ((value - istart)/(istop-istart));
    }

   virtual void onProcess(Graphics &g) {
          float frequency = getInternalParameterValue("frequency");
          float amplitude = getInternalParameterValue("amplitude");
          float attack = getInternalParameterValue("attackTime");
          //std::cout << attack <<std::endl;
          if(attack < 0.05){
              frame++;
              //std::cout<<peg<<std::endl;
          } else {
              f++;
          }
          g.pushMatrix();
          //g.translate(amplitude,  amplitude, -4);
          float y = map(frequency,  250,550,-0.75,0.75);
          float x =  map(frame%6,0,6,-1,1);
          g.translate(y,0, -4);

          //g.scale(frequency/2000, frequency/4000, 1);
          float hmamp = getInternalParameterValue("hmamp");
          //float scaling = 0.3 * map(sin(frame/10.0),-1,1,0,1);
          float scaling = 0.3;
          //g.scale(scaling * frequency/200, scaling * frequency/400, scaling* 1);
          g.scale(scaling,scaling * map(sin(frame/10.0),-1,1,0.5,1),scaling);
          g.color(mEnvFollow.value(), frequency/1000, mEnvFollow.value()* 10, 0.4);
          g.draw(mMesh);
          //g.draw(mesh2);
          g.popMatrix();
   }
    virtual void onTriggerOn() override {
        updateFromParameters();
        mAmpEnv.reset();
        mCFEnv.reset();
        mBWEnv.reset();
        
    }

    virtual void onTriggerOff() override {
        mAmpEnv.triggerRelease();
//        mCFEnv.triggerRelease();
//        mBWEnv.triggerRelease();
    }

    void updateFromParameters() {
        mOsc.freq(getInternalParameterValue("frequency"));
        mOsc.harmonics(getInternalParameterValue("hmnum"));
        mOsc.ampRatio(getInternalParameterValue("hmamp"));
        mAmpEnv.attack(getInternalParameterValue("attackTime"));
    //    mAmpEnv.decay(getInternalParameterValue("attackTime"));
        mAmpEnv.release(getInternalParameterValue("releaseTime"));
        mAmpEnv.levels()[1]=getInternalParameterValue("sustain");
        mAmpEnv.levels()[2]=getInternalParameterValue("sustain");

        mAmpEnv.curve(getInternalParameterValue("curve"));
        mPan.pos(getInternalParameterValue("pan"));
        mCFEnv.levels(getInternalParameterValue("cf1"),
                      getInternalParameterValue("cf2"),
                      getInternalParameterValue("cf1"));


        mCFEnv.lengths()[0] = getInternalParameterValue("cfRise");
        mCFEnv.lengths()[1] = 1 - getInternalParameterValue("cfRise");
        mBWEnv.levels(getInternalParameterValue("bw1"),
                      getInternalParameterValue("bw2"),
                      getInternalParameterValue("bw1"));
        mBWEnv.lengths()[0] = getInternalParameterValue("bwRise");
        mBWEnv.lengths()[1] = 1- getInternalParameterValue("bwRise");

        mCFEnv.totalLength(getInternalParameterValue("envDur"));
        mBWEnv.totalLength(getInternalParameterValue("envDur"));
    }
};



class MyApp : public App
{
public:
  SynthGUIManager<Sub> synthManager {"synth8"};
  //    ParameterMIDI parameterMIDI;
  bool major = true;
  bool seven = false;
  bool chords = false;
  int octave = 0;

  virtual void onInit( ) override {
    imguiInit();
    navControl().active(false);  // Disable navigation via keyboard, since we
                              // will be using keyboard for note triggering
    // Set sampling rate for Gamma objects from app's audio
    gam::sampleRate(audioIO().framesPerSecond());
  }

    void onCreate() override {
        // Play example sequence. Comment this line to start from scratch
        //    synthManager.synthSequencer().playSequence("synth8.synthSequence");
        synthManager.synthRecorder().verbose(true);
    }

    void onSound(AudioIOData& io) override {
        synthManager.render(io);  // Render audio
    }

    void onAnimate(double dt) override {
        imguiBeginFrame();
        synthManager.drawSynthControlPanel();
        imguiEndFrame();
    }

    void onDraw(Graphics& g) override {
        g.clear();
        synthManager.render(g);

        // Draw GUI
        imguiDraw();
    }
    void noteOn(int midiNote) {
        synthManager.voice()->setInternalParameterValue(
            "frequency", ::pow(2.f, (midiNote - 69.f) / 12.f) * 432.f);
        synthManager.triggerOn(midiNote);
    }

    

    bool onKeyDown(Keyboard const& k) override {
        if (ParameterGUI::usingKeyboard()) {  // Ignore keys if GUI is using them
        return true;
        }
        
        //std::cout << k.key() << std::endl;
        if (k.shift()) {
      // If shift pressed then keyboard sets preset
    //   int presetNumber = asciiToIndex(k.key());
    //   synthManager.recallPreset(presetNumber);
        major = !major;

    } else if (k.alt()){
        seven = !seven;
    } else if (k.ctrl()){
        //std::cout<< "chords\n";
        chords = !chords;

    } else if (k.key()  == 269){
        octave--;
    } else if (k.key() ==  271) {
        octave++;
    } else {
      // Otherwise trigger note for polyphonic synth
      int midiNote = asciiToMIDI(k.key()) + octave*12;
      
      if (midiNote > 0) {
          std::cout<< midiToNote(midiNote) << " ";
        noteOn(midiNote);
        
        if(chords){
            if(major){
                std::cout<<"major ";
                noteOn(midiNote + 4);
                if(seven){
                    std::cout << "7";
                    noteOn(midiNote + 11);
                }
            } else { //minor
                std::cout << "minor ";
                noteOn(midiNote + 3);
                if(seven){
                    std::cout << "7";
                    noteOn(midiNote + 10);
                }
            }
            std::cout<<"\n";

            noteOn(midiNote + 7);
        }
        
      }
    }
    return true;
  
    }

    bool onKeyUp(Keyboard const& k) override {
        int midiNote = asciiToMIDI(k.key()) + octave*12;
        if (midiNote > 0) {
            synthManager.triggerOff(midiNote);

            if(chords){
                synthManager.triggerOff(midiNote + 7);
                if(major) {
                    synthManager.triggerOff(midiNote + 4);
                    if(seven) synthManager.triggerOff(midiNote + 11);
                } else {
                    synthManager.triggerOff(midiNote + 3);
                    if(seven) synthManager.triggerOff(midiNote + 10);
                }
            }
        }
        return true;
    }

  void onExit() override { imguiShutdown(); }
};

int main() {
  MyApp app;

  // Set up audio
  app.configureAudio(48000., 512, 2, 0);

  app.start();
}
