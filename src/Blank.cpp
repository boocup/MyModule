#include "plugin.hpp"

using namespace rack;
using namespace rack::componentlibrary;
using namespace rack::ui;

struct BlankModule : Module {
    enum ParamId {
        BUTTON_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_OUTPUT,    // Stepped CV
        TRIG_OUTPUT,   // Trigger
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN   // <-- no lights now
    };

    // Visualization value 0..1 for the meter
    float vizLevel = 0.f;

    // Timer for rate-limited logging
    float logTimer = 0.f;

    // 3-step loop state
    bool loopRunning = false;
    int  loopStep    = 0;      // 0,1,2
    float loopTimer  = 0.f;    // seconds

    // trigger pulse state
    float trigTimer  = 0.f;    // seconds remaining in current trigger

    // button edge detector
    dsp::SchmittTrigger buttonTrig;

    BlankModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(BUTTON_PARAM, 0.f, 1.f, 0.f, "Loop on/off");

        configInput(IN_INPUT, "Tempo CV in");

        configOutput(OUT_OUTPUT,  "Stepped CV");
        configOutput(TRIG_OUTPUT, "Trigger");
    }

    void process(const ProcessArgs& args) override {

        //
        // BUTTON TOGGLE
        //
        if (buttonTrig.process(params[BUTTON_PARAM].getValue())) {
            loopRunning = !loopRunning;
        }

        //
        // READ INPUT FOR TEMPO + METER DISPLAY
        //
        float in = 0.f;
        if (inputs[IN_INPUT].isConnected())
            in = inputs[IN_INPUT].getVoltage();

        // map input to tempo speed -5V..+5V → 1.0s..0.1s
        float tempoV = in;
        float norm = clamp((tempoV + 5.f) / 10.f, 0.f, 1.f);
        const float maxStepTime = 1.0f;
        const float minStepTime = 0.1f;
        float stepTime = maxStepTime - (maxStepTime - minStepTime) * norm;

        // meter visualization from |in|
        const float fullScale = 5.f;
        float level = std::min(std::fabs(in) / fullScale, 1.f);
        vizLevel = 0.8f * level + 0.2f * vizLevel;

        //
        // LOOP + TRIGGER LOGIC
        //
        float out = 0.f;     // CV out
        float trigOut = 0.f; // trigger out

        // decay existing trigger
        if (trigTimer > 0.f) {
            trigTimer -= args.sampleTime;
            if (trigTimer < 0.f) trigTimer = 0.f;
        }

        if (loopRunning) {
            loopTimer += args.sampleTime;
            if (loopTimer >= stepTime) {
                loopTimer -= stepTime;
                loopStep = (loopStep + 1) % 3;

                // fire trigger pulse
                trigTimer = 0.01f;  // 10 ms
            }

            // hard-coded 3-step CV
            float seqVals[3] = {0.f, 2.f, 4.f};
            out = seqVals[loopStep];
        }
        else {
            loopTimer = 0.f;
            loopStep = 0;
            trigTimer = 0.f;
        }

        if (trigTimer > 0.f)
            trigOut = 10.f;

        //
        // SEND OUTPUTS
        //
        outputs[OUT_OUTPUT].setVoltage(out);
        outputs[TRIG_OUTPUT].setVoltage(trigOut);

        //
        // RATE-LIMITED DEBUG LOGGING
        //
        logTimer += args.sampleTime;
        if (logTimer >= 0.1f) {
            logTimer = 0.f;
            INFO("IN=%.3f  stepTime=%.3f  loop=%d  step=%d  CV=%.3f  TRIG=%.1f",
                 in, stepTime, (int)loopRunning, loopStep, out, trigOut);
        }
    }
};


// ============================
//   VERTICAL METER WIDGET
// ============================

struct VerticalMeter : TransparentWidget {
    BlankModule* module = nullptr;

    void draw(const DrawArgs& args) override {
        if (!module)
            return;

        float level = clamp(module->vizLevel, 0.f, 1.f);

        NVGcontext* vg = args.vg;
        Vec size = box.size;

        // background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, size.x, size.y, 2);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 80));
        nvgFill(vg);

        // filled section
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
        nvgStrokeColor(vg, nvgRGBA(200, 255, 255, 180));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
};


// ============================
//     MODULE WIDGET LAYOUT
// ============================

struct BlankModuleWidget : ModuleWidget {
    BlankModuleWidget(BlankModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Blank.svg")));

        // screws
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float jackX = 12.f;
        const float ctrlX = 21.f;
        const float row1Y = 60.f;
        const float row2Y = 90.f;

        //
        // BUTTON — moved left
        //
        addParam(createParamCentered<TL1105>(
            mm2px(Vec(ctrlX - 6.f, 45.f)),
            module,
            BlankModule::BUTTON_PARAM
        ));

        //
        // IN jack
        //
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(jackX, row1Y)),
            module,
            BlankModule::IN_INPUT
        ));

        //
        // VERTICAL METER
        //
        {
            Vec meterSizeMm    = Vec(4.f, 20.f);
            Vec meterTopLeftMm = Vec(
                ctrlX - meterSizeMm.x * 0.5f - 3.f,
                row1Y - meterSizeMm.y * 0.5f
            );

            auto* meter = createWidget<VerticalMeter>(mm2px(meterTopLeftMm));
            meter->box.size = mm2px(meterSizeMm);
            meter->module   = module;
            addChild(meter);
        }

        //
        // OUT jacks (CV + Trigger)
        //
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(jackX, row2Y)),
            module,
            BlankModule::OUT_OUTPUT
        ));

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(ctrlX, row2Y)),
            module,
            BlankModule::TRIG_OUTPUT
        ));

        //
        // TITLE
        //
        {
            auto* label = createWidget<Label>(mm2px(Vec(13.5f, 25.f)));
            label->text = "thereelpeet";
            label->color = nvgRGB(0x27, 0x27, 0x27);
            addChild(label);
        }
    }
};


Model* modelBlank = createModel<BlankModule, BlankModuleWidget>("blank");