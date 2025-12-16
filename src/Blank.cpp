#include "plugin.hpp"

using namespace rack;
using namespace rack::componentlibrary;
using namespace rack::ui;

// =======================
//   MODULE DEFINITION
// =======================

struct BlankModule : Module {
  enum ParamId {
    BUTTON_A_PARAM,
    RAND_A_PARAM,
    LENGTH_A_PARAM,

    BUTTON_B_PARAM,
    RAND_B_PARAM,
    LENGTH_B_PARAM,

    BPM_A_PARAM,
    BPM_B_PARAM,

    PARAMS_LEN
  };
  enum InputId {
    IN_A_INPUT,
    IN_B_INPUT,

    RNDTRIG_A_INPUT,
    RNDTRIG_B_INPUT,

    RUN_A_INPUT, // NEW: gate forces lane A run state
    RUN_B_INPUT, // NEW: gate forces lane B run state

    INPUTS_LEN
  };
  enum OutputId {
    OUT_A_OUTPUT,
    TRIG_A_OUTPUT,
    OUT_B_OUTPUT,
    TRIG_B_OUTPUT,
    OUTPUTS_LEN
  };
  enum LightId { RUN_A_LIGHT, RUN_B_LIGHT, LIGHTS_LEN };

  float vizTempoA = 0.f;
  float vizTempoB = 0.f;

  bool runningA = false;
  int stepA = 0;
  float timerA = 0.f;
  float trigTimerA = 0.f;
  float seqA[16];

  bool runningB = false;
  int stepB = 0;
  float timerB = 0.f;
  float trigTimerB = 0.f;
  float seqB[16];

  int lenA = 2;
  int lenB = 2;

  dsp::SchmittTrigger onA, randA, randInA;
  dsp::SchmittTrigger onB, randB, randInB;

  BlankModule() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    configParam(BUTTON_A_PARAM, 0.f, 1.f, 0.f, "Loop A on/off");
    configParam(RAND_A_PARAM, 0.f, 1.f, 0.f, "Randomize A");
    configParam(LENGTH_A_PARAM, 2.f, 16.f, 3.f, "Length A");

    configParam(BUTTON_B_PARAM, 0.f, 1.f, 0.f, "Loop B on/off");
    configParam(RAND_B_PARAM, 0.f, 1.f, 0.f, "Randomize B");
    configParam(LENGTH_B_PARAM, 2.f, 16.f, 3.f, "Length B");

    configParam(BPM_A_PARAM, 20.f, 300.f, 120.f, "BPM A");
    configParam(BPM_B_PARAM, 20.f, 300.f, 120.f, "BPM B");

    configInput(IN_A_INPUT, "Tempo CV A");
    configInput(IN_B_INPUT, "Tempo CV B");
    configInput(RNDTRIG_A_INPUT, "Random trigger A");
    configInput(RNDTRIG_B_INPUT, "Random trigger B");

    configInput(RUN_A_INPUT, "Run gate A");
    configInput(RUN_B_INPUT, "Run gate B");

    configOutput(OUT_A_OUTPUT, "CV A");
    configOutput(TRIG_A_OUTPUT, "Trigger A");
    configOutput(OUT_B_OUTPUT, "CV B");
    configOutput(TRIG_B_OUTPUT, "Trigger B");

    configLight(RUN_A_LIGHT, "Lane A running");
    configLight(RUN_B_LIGHT, "Lane B running");

    for (int i = 0; i < 16; i++) {
      seqA[i] = random::uniform() * 5.f;
      seqB[i] = random::uniform() * 5.f;
    }
  }

  void randomize(float *seq) {
    for (int i = 0; i < 16; i++)
      seq[i] = random::uniform() * 5.f;
  }

  void processLane(bool &running, int &step, float &timer, float &trigTimer,
                   float bpm, float *seq, int length, float &outCV,
                   float &outTrig, dsp::SchmittTrigger &onTrig,
                   dsp::SchmittTrigger &randTrig,
                   dsp::SchmittTrigger &randInTrig, float onVal, float randVal,
                   float randInV, bool runGateConnected, float runGateV,
                   const ProcessArgs &args) {

    // NEW: if run gate is patched, it *forces* running state
    if (runGateConnected) {
      running = (runGateV >= 1.f);
    } else {
      if (onTrig.process(onVal))
        running = !running;
    }

    bool doRand = false;
    if (randTrig.process(randVal))
      doRand = true;
    if (randInTrig.process(randInV))
      doRand = true;
    if (doRand)
      randomize(seq);

    // safe clamp
    bpm = clamp(bpm, 20.f, 300.f);

    // one step per beat
    float stepTime = 60.f / bpm;

    outCV = 0.f;
    outTrig = 0.f;

    if (trigTimer > 0.f) {
      trigTimer -= args.sampleTime;
      if (trigTimer < 0.f)
        trigTimer = 0.f;
    }

    if (running) {
      timer += args.sampleTime;
      if (timer >= stepTime) {
        timer -= stepTime;
        step = (step + 1) % length;
        trigTimer = 0.01f;
      }
      outCV = seq[step];
    } else {
      timer = 0.f;
      step = 0;
      trigTimer = 0.f;
    }

    if (trigTimer > 0.f)
      outTrig = 10.f;
  }

  void process(const ProcessArgs &args) override {
    // Random trigger CV inputs
    float rndInA = inputs[RNDTRIG_A_INPUT].isConnected()
                       ? inputs[RNDTRIG_A_INPUT].getVoltage()
                       : 0.f;
    float rndInB = inputs[RNDTRIG_B_INPUT].isConnected()
                       ? inputs[RNDTRIG_B_INPUT].getVoltage()
                       : 0.f;

    // Length params (kept as module members for displays)
    lenA = clamp((int)std::round(params[LENGTH_A_PARAM].getValue()), 1, 16);
    lenB = clamp((int)std::round(params[LENGTH_B_PARAM].getValue()), 1, 16);

    float outA = 0.f, trigAout = 0.f;
    float outB = 0.f, trigBout = 0.f;

    // BPM params
    float bpmA = params[BPM_A_PARAM].getValue();
    float bpmB = params[BPM_B_PARAM].getValue();

    // Run gate inputs (force run state when patched)
    bool runAConn = inputs[RUN_A_INPUT].isConnected();
    bool runBConn = inputs[RUN_B_INPUT].isConnected();
    float runAV = runAConn ? inputs[RUN_A_INPUT].getVoltage() : 0.f;
    float runBV = runBConn ? inputs[RUN_B_INPUT].getVoltage() : 0.f;

    // BPM CV overrides knob when patched (and moves knob to match)
    if (inputs[IN_A_INPUT].isConnected()) {
      float v = clamp(inputs[IN_A_INPUT].getVoltage(), 0.f, 10.f);
      bpmA = 20.f + (v / 10.f) * (300.f - 20.f);
      params[BPM_A_PARAM].setValue(bpmA);
    }
    if (inputs[IN_B_INPUT].isConnected()) {
      float v = clamp(inputs[IN_B_INPUT].getVoltage(), 0.f, 10.f);
      bpmB = 20.f + (v / 10.f) * (300.f - 20.f);
      params[BPM_B_PARAM].setValue(bpmB);
    }

    // Process lanes
    processLane(runningA, stepA, timerA, trigTimerA, bpmA, seqA, lenA, outA,
                trigAout, onA, randA, randInA,
                params[BUTTON_A_PARAM].getValue(),
                params[RAND_A_PARAM].getValue(), rndInA, runAConn, runAV, args);

    processLane(runningB, stepB, timerB, trigTimerB, bpmB, seqB, lenB, outB,
                trigBout, onB, randB, randInB,
                params[BUTTON_B_PARAM].getValue(),
                params[RAND_B_PARAM].getValue(), rndInB, runBConn, runBV, args);

    // Outputs
    outputs[OUT_A_OUTPUT].setVoltage(outA);
    outputs[TRIG_A_OUTPUT].setVoltage(trigAout);
    outputs[OUT_B_OUTPUT].setVoltage(outB);
    outputs[TRIG_B_OUTPUT].setVoltage(trigBout);

    // Lights
    lights[RUN_A_LIGHT].setBrightness(runningA ? 1.f : 0.f);
    lights[RUN_B_LIGHT].setBrightness(runningB ? 1.f : 0.f);
  }
};

// =======================
//   VERTICAL TEMPO METER
// =======================

struct VerticalTempoMeter : TransparentWidget {
  BlankModule *module = nullptr;
  bool isB = false;

  void draw(const DrawArgs &args) override {
    if (!module)
      return;

    float level = isB ? module->vizTempoB : module->vizTempoA;
    level = clamp(level, 0.f, 1.f);

    NVGcontext *vg = args.vg;
    Vec size = box.size;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, size.x, size.y, 2);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 70));
    nvgFill(vg);

    float filledH = size.y * level;
    float y = size.y - filledH;

    if (filledH > 1.f) {
      nvgBeginPath(vg);
      nvgRoundedRect(vg, 1, y + 1, size.x - 2, filledH - 2, 1.5);

      NVGpaint grad =
          nvgLinearGradient(vg, 0, y, 0, size.y, nvgRGBA(80, 220, 255, 255),
                            nvgRGBA(0, 90, 190, 255));
      nvgFillPaint(vg, grad);
      nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5, 0.5, size.x - 1, size.y - 1, 2);
    nvgStrokeColor(vg, nvgRGBA(200, 255, 255, 160));
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
  }
};

struct LengthDisplay : TransparentWidget {
  int *value = nullptr;

  void draw(const DrawArgs &args) override {
    if (!value)
      return;

    nvgFontFaceId(args.vg, APP->window->uiFont->handle);
    nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", *value);

    // number
    nvgFontSize(args.vg, 10.f); // was 9
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.35f, buf, nullptr);

    // label (moved up)
    nvgFontSize(args.vg, 9.f); // was 8
    nvgText(args.vg, box.size.x * 0.5f,
            box.size.y * 0.65f, // was 0.75
            "Steps", nullptr);
  }
};

struct BPMDisplay : TransparentWidget {
  Param *param = nullptr;

  void draw(const DrawArgs &args) override {
    if (!param)
      return;

    int bpm = (int)std::round(param->getValue());

    nvgFontFaceId(args.vg, APP->window->uiFont->handle);
    nvgFillColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", bpm);

    // number
    nvgFontSize(args.vg, 9.f);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.35f, buf, nullptr);

    // label
    nvgFontSize(args.vg, 8.f);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.65f, "BPM", nullptr);
  }
};

struct LengthParamDisplay : TransparentWidget {
  Module *module = nullptr;
  int paramId = -1;

  void draw(const DrawArgs &args) override {
    if (!module)
      return;

    int v = (int)std::round(module->params[paramId].getValue());

    nvgFontSize(args.vg, 11.f);
    nvgFontFaceId(args.vg, APP->window->uiFont->handle);
    nvgFillColor(args.vg, nvgRGB(0xcc, 0x33, 0x33));
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, buf, nullptr);
  }
};
struct LockOnCableKnob : RoundBlackKnob {
  BlankModule *m = nullptr;
  int inputId = -1;

  bool locked() const {
    return (m && inputId >= 0 && m->inputs[inputId].isConnected());
  }

  // void onChange(const event::Change& e) override {
  //     if (locked())
  //         return; // ignore user edits while CV is plugged
  //     RoundBlackKnob::onChange(e);
  // }
};

// =======================
//   WIDGET LAYOUT
// =======================

struct BlankModuleWidget : ModuleWidget {
  BlankModuleWidget(BlankModule *module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Blank.svg")));

    addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(
        createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ThemedScrew>(
        Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ThemedScrew>(Vec(
        box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    const float colA = 21.f;
    const float colB = 41.f;

    const float onY = 20.f;
    const float randY = 32.f;
    const float knobY = 46.f;
    const float bpmKnobY = 66.f;

    // const float meterY = 66.f;
    const float inY = 83.f;
    const float outY = 99.f;

    const float outDX = 4.5f;
    const float laneAX = colA - 7.f;
    const float laneBX = colB - 4.5f;

    addParam(createParamCentered<LEDButton>(mm2px(Vec(colA - 7.f, onY)), module,
                                            BlankModule::BUTTON_A_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(
        mm2px(Vec(colA - 7.f, onY)), module, BlankModule::RUN_A_LIGHT));

    addParam(createParamCentered<LEDButton>(
        mm2px(Vec(colB - 4.5f, onY)), module, BlankModule::BUTTON_B_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(
        mm2px(Vec(colB - 4.5f, onY)), module, BlankModule::RUN_B_LIGHT));

    addParam(createParamCentered<TL1105>(mm2px(Vec(colA - 7.f, randY)), module,
                                         BlankModule::RAND_A_PARAM));
    addParam(createParamCentered<TL1105>(mm2px(Vec(colB - 4.5f, randY)), module,
                                         BlankModule::RAND_B_PARAM));

    addParam(createParamCentered<RoundBlackKnob>(
        mm2px(Vec(colA - 7.f, knobY)), module, BlankModule::LENGTH_A_PARAM));
    addParam(createParamCentered<RoundBlackKnob>(
        mm2px(Vec(colB - 4.5f, knobY)), module, BlankModule::LENGTH_B_PARAM));
    auto *kbA = createParamCentered<LockOnCableKnob>(
        mm2px(Vec(laneAX, bpmKnobY)), module, BlankModule::BPM_A_PARAM);
    kbA->m = module;
    kbA->inputId = BlankModule::IN_A_INPUT;
    addParam(kbA);

    auto *kbB = createParamCentered<LockOnCableKnob>(
        mm2px(Vec(laneBX, bpmKnobY)), module, BlankModule::BPM_B_PARAM);
    kbB->m = module;
    kbB->inputId = BlankModule::IN_B_INPUT;
    addParam(kbB);

    // --- Shared lane geometry ---
    const float laneACenterX = laneAX;
    const float laneBCenterX = laneBX;

    const float cvXOffset = outDX; // left/right split for the two output jacks
    const float rndTrigY =
        outY + 12.f; // random trigger input row (under the 1v/O input)

    // ---------------------------
    // Lane A
    // ---------------------------

    // tempo CV input (top input for lane)
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneACenterX, inY)),
                                             module, BlankModule::IN_A_INPUT));

    // outputs: CV + Gate (split left/right)
    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX - cvXOffset, outY)), module,
        BlankModule::OUT_A_OUTPUT));

    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX + cvXOffset, outY)), module,
        BlankModule::TRIG_A_OUTPUT));

    // random trigger input (centered, under 1v/O input)
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX - cvXOffset, rndTrigY)), module,
        BlankModule::RNDTRIG_A_INPUT));

    // ---------------------------
    // Lane B
    // ---------------------------

    // tempo CV input
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneBCenterX, inY)),
                                             module, BlankModule::IN_B_INPUT));

    // outputs: CV + Gate
    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX - cvXOffset, outY)), module,
        BlankModule::OUT_B_OUTPUT));

    addOutput(createOutputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX + cvXOffset, outY)), module,
        BlankModule::TRIG_B_OUTPUT));

    // random trigger input
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX - cvXOffset, rndTrigY)), module,
        BlankModule::RNDTRIG_B_INPUT));

    if (module) {
      const float dispW = 12.f;
      // const float dispH = 8.f;
      const float labelY =
          50.f; // <-- set this to where the old "Len" label was

      auto *lenADisplay = new LengthDisplay;
      lenADisplay->box.pos = mm2px(Vec(laneAX - dispW * 0.5f, labelY));
      lenADisplay->box.size = mm2px(Vec(12.f, 11.f));
      lenADisplay->value = &module->lenA;
      addChild(lenADisplay);

      auto *lenBDisplay = new LengthDisplay;
      lenBDisplay->box.pos = mm2px(Vec(laneBX - dispW * 0.5f, labelY));
      lenBDisplay->box.size = mm2px(Vec(12.f, 11.f));
      lenBDisplay->value = &module->lenB;
      addChild(lenBDisplay);
    }

    if (module) {
      auto *bpmDispA = new BPMDisplay();
      bpmDispA->param = &module->params[BlankModule::BPM_A_PARAM];
      bpmDispA->box.pos = mm2px(Vec(laneAX - 6.f, bpmKnobY + 4.f));
      bpmDispA->box.size = mm2px(Vec(12.f, 11.f));
      addChild(bpmDispA);

      auto *bpmDispB = new BPMDisplay();
      bpmDispB->param = &module->params[BlankModule::BPM_B_PARAM];
      bpmDispB->box.pos = mm2px(Vec(laneBX - 6.f, bpmKnobY + 4.f));
      bpmDispB->box.size = mm2px(Vec(12.f, 11.f));
      addChild(bpmDispB);
    }

    // Lane A: run gate input (to the right of RND input)
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneACenterX + cvXOffset, rndTrigY)), module,
        BlankModule::RUN_A_INPUT));

    // Lane B: run gate input
    addInput(createInputCentered<PJ301MPort>(
        mm2px(Vec(laneBCenterX + cvXOffset, rndTrigY)), module,
        BlankModule::RUN_B_INPUT));
  }
};

Model *modelBlank = createModel<BlankModule, BlankModuleWidget>("blank");
