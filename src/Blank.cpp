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
        BUTTON_B_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_A_INPUT,
        IN_B_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_A_OUTPUT,    // Stepped CV A
        TRIG_A_OUTPUT,   // Trigger A
        OUT_B_OUTPUT,    // Stepped CV B
        TRIG_B_OUTPUT,   // Trigger B
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN   // no lights
    };

    // Visualization values 0..1 for meters
    float vizLevelA = 0.f;
    float vizLevelB = 0.f;

    // Timer for rate-limited logging
    float logTimer = 0.f;

    // Loop A state
    bool  loopRunningA = false;
    int   loopStepA    = 0;
    float loopTimerA   = 0.f;
    float trigTimerA   = 0.f;
    dsp::SchmittTrigger buttonTrigA;

    // Loop B state
    bool  loopRunningB = false;
    int   loopStepB    = 0;
    float loopTimerB   = 0.f;
    float trigTimerB   = 0.f;
    dsp::SchmittTrigger buttonTrigB;

    BlankModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(BUTTON_A_PARAM, 0.f, 1.f, 0.f, "Loop A on/off");
        configParam(BUTTON_B_PARAM, 0.f, 1.f, 0.f, "Loop B on/off");

        configInput(IN_A_INPUT, "Tempo CV A");
        configInput(IN_B_INPUT, "Tempo CV B");

        configOutput(OUT_A_OUTPUT,  "Stepped CV A");
        configOutput(TRIG_A_OUTPUT, "Trigger A");
        configOutput(OUT_B_OUTPUT,  "Stepped CV B");
        configOutput(TRIG_B_OUTPUT, "Trigger B");
    }

    void processLoop(
        // per-loop IO/state
        bool& loopRunning,
        int& loopStep,
        float& loopTimer,
        float& trigTimer,
        float inV,
        float& vizLevel,
        float& outCV,
        float& outTrig,
        dsp::SchmittTrigger& buttonTrig,
        float buttonParam,
        const ProcessArgs& args
    ) {
        // Button toggle
        if (buttonTrig.process(buttonParam)) {
            loopRunning = !loopRunning;
        }

        // Tempo mapping: -5..+5 V -> 1.0..0.1 s
        float tempoV = inV;
        float norm = clamp((tempoV + 5.f) / 10.f, 0.f, 1.f);
        const float maxStepTime = 1.0f;
        const float minStepTime = 0.1f;
        float stepTime = maxStepTime - (maxStepTime - minStepTime) * norm;

        // Meter level from |in|
        const float fullScale = 5.f;
        float level = std::min(std::fabs(inV) / fullScale, 1.f);
        vizLevel = 0.8f * level + 0.2f * vizLevel;

        // Default outputs
        outCV   = 0.f;
        outTrig = 0.f;

        // Decay existing trigger
        if (trigTimer > 0.f) {
            trigTimer -= args.sampleTime;
            if (trigTimer < 0.f)
                trigTimer = 0.f;
        }

        if (loopRunning) {
            loopTimer += args.sampleTime;
            if (loopTimer >= stepTime) {
                loopTimer -= stepTime;
                loopStep = (loopStep + 1) % 3;

                // Fire trigger for this step
                trigTimer = 0.01f; // 10 ms
            }

            float seqVals[3] = {0.f, 2.f, 4.f};
            outCV = seqVals[loopStep];
        }
        else {
            loopTimer = 0.f;
            loopStep  = 0;
            trigTimer = 0.f;
        }

        if (trigTimer > 0.f)
            outTrig = 10.f;
    }

    void process(const ProcessArgs& args) override {
        // Read inputs
        float inA = 0.f;
        if (inputs[IN_A_INPUT].isConnected())
            inA = inputs[IN_A_INPUT].getVoltage();

        float inB = 0.f;
        if (inputs[IN_B_INPUT].isConnected())
            inB = inputs[IN_B_INPUT].getVoltage();

        // Outputs per loop
        float outA = 0.f, trigA = 0.f;
        float outB = 0.f, trigB = 0.f;

        // Process both loops
        processLoop(
            loopRunningA, loopStepA, loopTimerA, trigTimerA,
            inA, vizLevelA, outA, trigA, buttonTrigA,
            params[BUTTON_A_PARAM].getValue(), args
        );

        processLoop(
            loopRunningB, loopStepB, loopTimerB, trigTimerB,
            inB, vizLevelB, outB, trigB, buttonTrigB,
            params[BUTTON_B_PARAM].getValue(), args
        );

        // Send outputs
        outputs[OUT_A_OUTPUT].setVoltage(outA);
        outputs[TRIG_A_OUTPUT].setVoltage(trigA);
        outputs[OUT_B_OUTPUT].setVoltage(outB);
        outputs[TRIG_B_OUTPUT].setVoltage(trigB);

        // Rate-limited log
        logTimer += args.sampleTime;
        if (logTimer >= 0.1f) {
            logTimer = 0.f;
            INFO("A: in=%.3f step=%d running=%d cv=%.3f trig=%.1f | B: in=%.3f step=%d running=%d cv=%.3f trig=%.1f",
                 inA, loopStepA, (int)loopRunningA, outA, trigA,
                 inB, loopStepB, (int)loopRunningB, outB, trigB);
        }
    }
};


// =======================
//   VERTICAL METER
// =======================

struct VerticalMeter : TransparentWidget {
    BlankModule* module = nullptr;
    bool rightSide = false;  // false = A, true = B

    void draw(const DrawArgs& args) override {
        if (!module)
            return;

        float level = rightSide ? module->vizLevelB : module->vizLevelA;
        level = clamp(level, 0.f, 1.f);

        NVGcontext* vg = args.vg;
        Vec size = box.size;

        // Background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, size.x, size.y, 2);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 80));
        nvgFill(vg);

        // Fill
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

        // Border
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0.5, 0.5, size.x - 1, size.y - 1, 2);
        nvgStrokeColor(vg, nvgRGBA(200, 255, 255, 180));
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

        // Screws
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout constants (mm)
        const float jackX1 = 12.f;
        const float ctrlX1 = 21.f;

        const float jackX2 = 32.f;   // right side column
        const float ctrlX2 = 41.f;

        const float row1Y  = 60.f;   // IN + meter
        const float row2Y  = 90.f;   // OUT jacks

        // ----- Buttons -----
        addParam(createParamCentered<TL1105>(
            mm2px(Vec(ctrlX1 - 6.f, 45.f)),
            module,
            BlankModule::BUTTON_A_PARAM
        ));
        addParam(createParamCentered<TL1105>(
            mm2px(Vec(ctrlX2 - 6.f, 45.f)),
            module,
            BlankModule::BUTTON_B_PARAM
        ));

        // ----- Row 1: IN jacks -----
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(jackX1, row1Y)),
            module,
            BlankModule::IN_A_INPUT
        ));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(jackX2, row1Y)),
            module,
            BlankModule::IN_B_INPUT
        ));

        // ----- Meters -----
        {
            Vec meterSizeMm = Vec(4.f, 20.f);

            // Left meter (A)
            {
                Vec topLeftMm(
                    ctrlX1 - meterSizeMm.x * 0.5f - 3.f,
                    row1Y - meterSizeMm.y * 0.5f
                );
                auto* meterA = createWidget<VerticalMeter>(mm2px(topLeftMm));
                meterA->box.size = mm2px(meterSizeMm);
                meterA->module   = module;
                meterA->rightSide = false;
                addChild(meterA);
            }

            // Right meter (B)
            {
                Vec topLeftMm(
                    ctrlX2 - meterSizeMm.x * 0.5f - 3.f,
                    row1Y - meterSizeMm.y * 0.5f
                );
                auto* meterB = createWidget<VerticalMeter>(mm2px(topLeftMm));
                meterB->box.size = mm2px(meterSizeMm);
                meterB->module   = module;
                meterB->rightSide = true;
                addChild(meterB);
            }
        }

        // ----- Row 2: OUT jacks -----
        // Left pair: CV A + TRIG A
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(jackX1, row2Y)),
            module,
            BlankModule::OUT_A_OUTPUT
        ));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ctrlX1, row2Y)),
            module,
            BlankModule::TRIG_A_OUTPUT
        ));

        // Right pair: CV B + TRIG B
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(jackX2, row2Y)),
            module,
            BlankModule::OUT_B_OUTPUT
        ));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ctrlX2, row2Y)),
            module,
            BlankModule::TRIG_B_OUTPUT
        ));

        // Title
        {
            auto* label = createWidget<Label>(mm2px(Vec(13.5f, 25.f)));
            label->text = "thereelpeet";
            label->color = nvgRGB(0x27, 0x27, 0x27);
            addChild(label);
        }
    }
};

Model* modelBlank = createModel<BlankModule, BlankModuleWidget>("blank");