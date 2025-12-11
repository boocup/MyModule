#include "plugin.hpp"

using namespace rack;
using namespace rack::componentlibrary;
using namespace rack::ui;

struct BlankModule : Module {
    enum ParamId {
        KNOB_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        IN_LIGHT,
        LIGHTS_LEN
    };

    // <-- You were missing this
    // Visualization value 0..1 for the meter / debug
    float vizLevel = 0.f;
    float logTimer = 0.f;

    BlankModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(KNOB_PARAM, 0.f, 10.f, 5.f, "Output voltage");
        configInput(IN_INPUT, "Monitor in");
        configOutput(OUT_OUTPUT, "DC out");
        configLight(IN_LIGHT, "Input level indicator");
    }

    void process(const ProcessArgs& args) override {
        //
        // OUT is always the knob value (0–10 V)
        //
        float out = params[KNOB_PARAM].getValue();
        outputs[OUT_OUTPUT].setVoltage(out);

        //
        // IN is only used for the meter
        //
        float in = 0.f;
        if (inputs[IN_INPUT].isConnected()) {
            in = inputs[IN_INPUT].getVoltage();
        }

        //
        // Map input voltage to 0–1
        //
        const float fullScale = 5.f;          // 5 V → full meter
        float level = std::min(std::fabs(in) / fullScale, 1.f);

        //
        // Smoothing (prevents flicker)
        //
        const float a = 0.8f;                 // closer to 1 = faster response
        vizLevel = a * level + (1.f - a) * vizLevel;

        //
        // LED brightness (use the smoothed value)
        //
        lights[IN_LIGHT].setBrightness(vizLevel);


        //
        // RATE-LIMITED LOGGING (about every 0.1 seconds)
        //
        logTimer += args.sampleTime;
        if (logTimer >= 0.1f) {
            logTimer = 0.f;

            float _in    = in;
            float _level = level;
            float _viz   = vizLevel;

            INFO("in=%.3f  level=%.3f  vizLevel=%.3f", _in, _level, _viz);
        }


    }
};


struct BlankModuleWidget : ModuleWidget {
    BlankModuleWidget(BlankModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Blank.svg")));

        // Screws
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Layout constants
        const float jackX = 12.f;    // left column (jacks)
        const float ctrlX = 21.f;    // right column (LED / knob)

        // Two rows, up on the panel
        const float row1Y = 60.f;    // IN + LED
        const float row2Y = 90.f;    // OUT + knob

        // ===== Row 1: IN jack + LED (monitor only) =====
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(jackX, row1Y)),
            module,
            BlankModule::IN_INPUT
        ));

        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(ctrlX, row1Y)),
            module,
            BlankModule::IN_LIGHT
        ));

        // ===== Row 2: OUT jack + KNOB (always tied together) =====
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(jackX, row2Y)),
            module,
            BlankModule::OUT_OUTPUT
        ));

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(ctrlX, row2Y)),
            module,
            BlankModule::KNOB_PARAM
        ));

        // Title label
        {
            auto* label = createWidget<Label>(mm2px(Vec(13.5f, 25.f)));
            label->text = "thereelpeet";
            label->color = nvgRGB(0x27, 0x27, 0x27);
            addChild(label);
        }
    }
};

Model* modelBlank = createModel<BlankModule, BlankModuleWidget>("blank");