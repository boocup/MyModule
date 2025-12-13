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

        PARAMS_LEN
    };
    enum InputId {
        IN_A_INPUT,
        IN_B_INPUT,

        RNDTRIG_A_INPUT,
        RNDTRIG_B_INPUT,

        INPUTS_LEN
    };
    enum OutputId {
        OUT_A_OUTPUT,
        TRIG_A_OUTPUT,
        OUT_B_OUTPUT,
        TRIG_B_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        RUN_A_LIGHT,
        RUN_B_LIGHT,
        LIGHTS_LEN
    };

    float vizTempoA = 0.f;
    float vizTempoB = 0.f;

    bool  runningA = false;
    int   stepA = 0;
    float timerA = 0.f;
    float trigTimerA = 0.f;
    float seqA[16];

    bool  runningB = false;
    int   stepB = 0;
    float timerB = 0.f;
    float trigTimerB = 0.f;
    float seqB[16];

    dsp::SchmittTrigger onA, randA, randInA;
    dsp::SchmittTrigger onB, randB, randInB;

    BlankModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(BUTTON_A_PARAM, 0.f, 1.f, 0.f, "Loop A on/off");
        configParam(RAND_A_PARAM,   0.f, 1.f, 0.f, "Randomize A");
        configParam(LENGTH_A_PARAM, 2.f, 16.f, 3.f, "Length A");

        configParam(BUTTON_B_PARAM, 0.f, 1.f, 0.f, "Loop B on/off");
        configParam(RAND_B_PARAM,   0.f, 1.f, 0.f, "Randomize B");
        configParam(LENGTH_B_PARAM, 2.f, 16.f, 3.f, "Length B");

        configInput(IN_A_INPUT, "Tempo CV A");
        configInput(IN_B_INPUT, "Tempo CV B");
        configInput(RNDTRIG_A_INPUT, "Random trigger A");
        configInput(RNDTRIG_B_INPUT, "Random trigger B");

        configOutput(OUT_A_OUTPUT,  "CV A");
        configOutput(TRIG_A_OUTPUT, "Trigger A");
        configOutput(OUT_B_OUTPUT,  "CV B");
        configOutput(TRIG_B_OUTPUT, "Trigger B");

        configLight(RUN_A_LIGHT, "Lane A running");
        configLight(RUN_B_LIGHT, "Lane B running");

        for (int i = 0; i < 16; i++) {
            seqA[i] = random::uniform() * 5.f;
            seqB[i] = random::uniform() * 5.f;
        }
    }

    void randomize(float* seq) {
        for (int i = 0; i < 16; i++)
            seq[i] = random::uniform() * 5.f;
    }

    void processLane(
        bool& running, int& step, float& timer, float& trigTimer,
        float inV, float& viz,
        float* seq, int length,
        float& outCV, float& outTrig,
        dsp::SchmittTrigger& onTrig,
        dsp::SchmittTrigger& randTrig,
        dsp::SchmittTrigger& randInTrig,
        float onVal, float randVal, float randInV,
        const ProcessArgs& args
    ) {
        if (onTrig.process(onVal))
            running = !running;

        bool doRand = false;

        if (randTrig.process(randVal))
            doRand = true;

        if (randInTrig.process(randInV))
            doRand = true;

        if (doRand)
            randomize(seq);

        float norm = clamp((inV + 5.f) / 10.f, 0.f, 1.f);
        float stepTime = 1.0f - 0.9f * norm;

        viz = 0.85f * norm + 0.15f * viz;

        outCV = 0.f;
        outTrig = 0.f;

        if (trigTimer > 0.f) {
            trigTimer -= args.sampleTime;
            if (trigTimer < 0.f) trigTimer = 0.f;
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

    void process(const ProcessArgs& args) override {
        float inA = inputs[IN_A_INPUT].isConnected() ? inputs[IN_A_INPUT].getVoltage() : 0.f;
        float inB = inputs[IN_B_INPUT].isConnected() ? inputs[IN_B_INPUT].getVoltage() : 0.f;

        float rndInA = inputs[RNDTRIG_A_INPUT].isConnected() ? inputs[RNDTRIG_A_INPUT].getVoltage() : 0.f;
        float rndInB = inputs[RNDTRIG_B_INPUT].isConnected() ? inputs[RNDTRIG_B_INPUT].getVoltage() : 0.f;

        int lenA = (int) std::round(params[LENGTH_A_PARAM].getValue());
        int lenB = (int) std::round(params[LENGTH_B_PARAM].getValue());

        float outA = 0.f, trigAout = 0.f;
        float outB = 0.f, trigBout = 0.f;

        processLane(runningA, stepA, timerA, trigTimerA,
                    inA, vizTempoA, seqA, lenA,
                    outA, trigAout,
                    onA, randA, randInA,
                    params[BUTTON_A_PARAM].getValue(),
                    params[RAND_A_PARAM].getValue(),
                    rndInA,
                    args);

        processLane(runningB, stepB, timerB, trigTimerB,
                    inB, vizTempoB, seqB, lenB,
                    outB, trigBout,
                    onB, randB, randInB,
                    params[BUTTON_B_PARAM].getValue(),
                    params[RAND_B_PARAM].getValue(),
                    rndInB,
                    args);

        outputs[OUT_A_OUTPUT].setVoltage(outA);
        outputs[TRIG_A_OUTPUT].setVoltage(trigAout);
        outputs[OUT_B_OUTPUT].setVoltage(outB);
        outputs[TRIG_B_OUTPUT].setVoltage(trigBout);

        lights[RUN_A_LIGHT].setBrightness(runningA ? 1.f : 0.f);
        lights[RUN_B_LIGHT].setBrightness(runningB ? 1.f : 0.f);
    }
};

// =======================
//   VERTICAL TEMPO METER
// =======================

struct VerticalTempoMeter : TransparentWidget {
    BlankModule* module = nullptr;
    bool isB = false;

    void draw(const DrawArgs& args) override {
        if (!module)
            return;

        float level = isB ? module->vizTempoB : module->vizTempoA;
        level = clamp(level, 0.f, 1.f);

        NVGcontext* vg = args.vg;
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

            NVGpaint grad = nvgLinearGradient(
                vg,
                0, y,
                0, size.y,
                nvgRGBA(80, 220, 255, 255),
                nvgRGBA(0, 90, 190, 255)
            );
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

// =======================
//   WIDGET LAYOUT
// =======================

struct BlankModuleWidget : ModuleWidget {
    BlankModuleWidget(BlankModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Blank.svg")));

        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float colA = 21.f;
        const float colB = 41.f;

        const float onY   = 20.f;
        const float randY = 32.f;
        const float knobY = 46.f;

        const float meterY = 66.f;
        const float inY    = 80.f;
        const float outY   = 95.f;

        const float outDX = 4.5f;
        const float laneAX = colA - 7.f;
        const float laneBX = colB - 4.5f;

        addParam(createParamCentered<LEDButton>(mm2px(Vec(colA - 7.f, onY)), module, BlankModule::BUTTON_A_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(colA - 7.f, onY)), module, BlankModule::RUN_A_LIGHT));

        addParam(createParamCentered<LEDButton>(mm2px(Vec(colB - 4.5f, onY)), module, BlankModule::BUTTON_B_PARAM));
        addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(colB - 4.5f, onY)), module, BlankModule::RUN_B_LIGHT));

        addParam(createParamCentered<TL1105>(mm2px(Vec(colA - 7.f, randY)), module, BlankModule::RAND_A_PARAM));
        addParam(createParamCentered<TL1105>(mm2px(Vec(colB - 4.5f, randY)), module, BlankModule::RAND_B_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(colA - 7.f, knobY)), module, BlankModule::LENGTH_A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(colB - 4.5f, knobY)), module, BlankModule::LENGTH_B_PARAM));

        {
            Vec meterSizeMm(4.f, 18.f);

            auto* mA = createWidget<VerticalTempoMeter>(
                mm2px(Vec(colA - 7.f - meterSizeMm.x * 0.5f, meterY - meterSizeMm.y * 0.5f))
            );
            mA->box.size = mm2px(meterSizeMm);
            mA->module = module;
            mA->isB = false;
            addChild(mA);

            auto* mB = createWidget<VerticalTempoMeter>(
                mm2px(Vec(colB - 4.5f - meterSizeMm.x * 0.5f, meterY - meterSizeMm.y * 0.5f))
            );
            mB->box.size = mm2px(meterSizeMm);
            mB->module = module;
            mB->isB = true;
            addChild(mB);
        }
        // --- Shared lane geometry ---
        const float laneACenterX = laneAX;
        const float laneBCenterX = laneBX;

        const float cvXOffset = outDX;          // left/right split for the two output jacks
        const float rndTrigY  = outY + 12.f;    // random trigger input row (under the 1v/O input)

        // ---------------------------
        // Lane A
        // ---------------------------

        // tempo CV input (top input for lane)
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(laneACenterX, inY)), module, BlankModule::IN_A_INPUT));

        // outputs: CV + Gate (split left/right)
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(laneACenterX - cvXOffset, outY)), module, BlankModule::OUT_A_OUTPUT));

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(laneACenterX + cvXOffset, outY)), module, BlankModule::TRIG_A_OUTPUT));

        // random trigger input (centered, under 1v/O input)
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(laneACenterX - cvXOffset, rndTrigY)), module, BlankModule::RNDTRIG_A_INPUT));

        // ---------------------------
        // Lane B
        // ---------------------------

        // tempo CV input
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(laneBCenterX, inY)), module, BlankModule::IN_B_INPUT));

        // outputs: CV + Gate
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(laneBCenterX - cvXOffset, outY)), module, BlankModule::OUT_B_OUTPUT));

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(laneBCenterX + cvXOffset, outY)), module, BlankModule::TRIG_B_OUTPUT));

        // random trigger input
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(laneBCenterX - cvXOffset, rndTrigY)), module, BlankModule::RNDTRIG_B_INPUT));
            }
        };

Model* modelBlank = createModel<BlankModule, BlankModuleWidget>("blank");