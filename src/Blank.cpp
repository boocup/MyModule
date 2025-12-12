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
        BUTTON_B_PARAM,
        RAND_B_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_A_INPUT,
        IN_B_INPUT,
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
        LIGHTS_LEN
    };

    // Tempo visualization (0..1) per lane
    float vizTempoA = 0.f;
    float vizTempoB = 0.f;

    // Loop A state
    bool  loopRunningA = false;
    int   loopStepA    = 0;
    float loopTimerA   = 0.f;
    float trigTimerA   = 0.f;
    float seqA[3]      = {0.f, 2.f, 4.f};

    // Loop B state
    bool  loopRunningB = false;
    int   loopStepB    = 0;
    float loopTimerB   = 0.f;
    float trigTimerB   = 0.f;
    float seqB[3]      = {0.f, 2.f, 4.f};

    // Button edge detectors
    dsp::SchmittTrigger onOffTrigA, randTrigA;
    dsp::SchmittTrigger onOffTrigB, randTrigB;

    BlankModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(BUTTON_A_PARAM, 0.f, 1.f, 0.f, "Loop A on/off");
        configParam(RAND_A_PARAM,   0.f, 1.f, 0.f, "Randomize A");

        configParam(BUTTON_B_PARAM, 0.f, 1.f, 0.f, "Loop B on/off");
        configParam(RAND_B_PARAM,   0.f, 1.f, 0.f, "Randomize B");

        configInput(IN_A_INPUT, "Tempo CV A");
        configInput(IN_B_INPUT, "Tempo CV B");

        configOutput(OUT_A_OUTPUT,  "CV A");
        configOutput(TRIG_A_OUTPUT, "Trigger A");
        configOutput(OUT_B_OUTPUT,  "CV B");
        configOutput(TRIG_B_OUTPUT, "Trigger B");
    }

    void randomizeSeq(float seq[3]) {
        // 0..5V random values (easy to feed Scales)
        for (int i = 0; i < 3; i++) {
            seq[i] = random::uniform() * 5.f;
        }
    }

    void processLane(
        bool& running, int& step, float& timer, float& trigTimer,
        float inV, float& vizTempo, float seq[3],
        float& outCV, float& outTrig,
        dsp::SchmittTrigger& onOffTrig,
        dsp::SchmittTrigger& randTrig,
        float onOffParam, float randParam,
        const ProcessArgs& args
    ) {
        // Toggle on/off
        if (onOffTrig.process(onOffParam))
            running = !running;

        // Randomize values
        if (randTrig.process(randParam))
            randomizeSeq(seq);

        // Tempo mapping: -5..+5 V -> 1.0..0.1 s per step
        float norm = clamp((inV + 5.f) / 10.f, 0.f, 1.f); // 0=slow, 1=fast
        float stepTime = 1.0f - 0.9f * norm;             // 1.0..0.1

        // Tempo indicator: show speed (norm), not amplitude
        vizTempo = 0.85f * norm + 0.15f * vizTempo;

        // Defaults
        outCV = 0.f;
        outTrig = 0.f;

        // Trigger decay
        if (trigTimer > 0.f) {
            trigTimer -= args.sampleTime;
            if (trigTimer < 0.f) trigTimer = 0.f;
        }

        if (running) {
            timer += args.sampleTime;
            if (timer >= stepTime) {
                timer -= stepTime;
                step = (step + 1) % 3;
                trigTimer = 0.01f; // 10 ms trigger
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

        float outA = 0.f, trigA = 0.f;
        float outB = 0.f, trigB = 0.f;

        processLane(loopRunningA, loopStepA, loopTimerA, trigTimerA,
                    inA, vizTempoA, seqA,
                    outA, trigA,
                    onOffTrigA, randTrigA,
                    params[BUTTON_A_PARAM].getValue(),
                    params[RAND_A_PARAM].getValue(),
                    args);

        processLane(loopRunningB, loopStepB, loopTimerB, trigTimerB,
                    inB, vizTempoB, seqB,
                    outB, trigB,
                    onOffTrigB, randTrigB,
                    params[BUTTON_B_PARAM].getValue(),
                    params[RAND_B_PARAM].getValue(),
                    args);

        outputs[OUT_A_OUTPUT].setVoltage(outA);
        outputs[TRIG_A_OUTPUT].setVoltage(trigA);
        outputs[OUT_B_OUTPUT].setVoltage(outB);
        outputs[TRIG_B_OUTPUT].setVoltage(trigB);
    }
};

// =======================
//   VERTICAL TEMPO METER
// =======================

struct VerticalTempoMeter : TransparentWidget {
    BlankModule* module = nullptr;
    bool isB = false; // false=A, true=B

    void draw(const DrawArgs& args) override {
        if (!module)
            return;

        float level = isB ? module->vizTempoB : module->vizTempoA;
        level = clamp(level, 0.f, 1.f);

        NVGcontext* vg = args.vg;
        Vec size = box.size;

        // background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, size.x, size.y, 2);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 70));
        nvgFill(vg);

        // filled portion bottom->top
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

        // border
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

        // Columns (mm)
        const float jackXA = 12.f;
        const float ctrlXA = 21.f;

        const float jackXB = 32.f;
        const float ctrlXB = 41.f;

        // Rows (mm)
        const float onY   = 38.f;
        const float randY = 50.f;
        const float inY   = 65.f;
        const float outY  = 95.f;

        // ON/OFF buttons (top row)
        addParam(createParamCentered<TL1105>(mm2px(Vec(ctrlXA - 6.f, onY)), module, BlankModule::BUTTON_A_PARAM));
        addParam(createParamCentered<TL1105>(mm2px(Vec(ctrlXB - 6.f, onY)), module, BlankModule::BUTTON_B_PARAM));

        // RANDOM buttons (underneath)
        addParam(createParamCentered<TL1105>(mm2px(Vec(ctrlXA - 6.f, randY)), module, BlankModule::RAND_A_PARAM));
        addParam(createParamCentered<TL1105>(mm2px(Vec(ctrlXB - 6.f, randY)), module, BlankModule::RAND_B_PARAM));

        // IN jacks
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jackXA, inY)), module, BlankModule::IN_A_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jackXB, inY)), module, BlankModule::IN_B_INPUT));

        // Tempo meters (next to each IN)
        {
            Vec meterSizeMm(4.f, 18.f);

            // A meter slightly right of A IN
            auto* mA = createWidget<VerticalTempoMeter>(mm2px(Vec(ctrlXA - 4.f, inY - meterSizeMm.y * 0.5f)));
            mA->box.size = mm2px(meterSizeMm);
            mA->module = module;
            mA->isB = false;
            addChild(mA);

            // B meter slightly right of B IN
            auto* mB = createWidget<VerticalTempoMeter>(mm2px(Vec(ctrlXB - 4.f, inY - meterSizeMm.y * 0.5f)));
            mB->box.size = mm2px(meterSizeMm);
            mB->module = module;
            mB->isB = true;
            addChild(mB);
        }

        // OUT jacks (CV + TRIG for each lane)
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jackXA, outY)), module, BlankModule::OUT_A_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(ctrlXA, outY)), module, BlankModule::TRIG_A_OUTPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jackXB, outY)), module, BlankModule::OUT_B_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(ctrlXB, outY)), module, BlankModule::TRIG_B_OUTPUT));
    }
};

Model* modelBlank = createModel<BlankModule, BlankModuleWidget>("blank");