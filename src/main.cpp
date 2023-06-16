#include <cstring>
#include <string>
#include <format>
#include <memory>

#include <switch.h>

#define TESLA_INIT_IMPL
#include <tesla.hpp>

class MasterVolumeGui: public tsl::Gui {
    public:
        constexpr static float MasterVolumeMin     = 0.0f;
        constexpr static float MasterVolumeMax     = 2.0f;
        constexpr static float MasterVolumeDefault = 1.0f;

    public:
        MasterVolumeGui() {
            audctlGetSystemOutputMasterVolume(&this->master_volume);
        }

        virtual tsl::elm::Element* createUI() override {
            auto *frame = new tsl::elm::OverlayFrame(APP_TITLE, APP_VERSION);

            auto *list = new tsl::elm::List();

            this->header = new tsl::elm::CategoryHeader("Master volume (max. 2)");
            this->slider = new tsl::elm::TrackBar("");

            this->slider->setProgress(this->master_volume * 100.0f / (MasterVolumeGui::MasterVolumeMax - MasterVolumeGui::MasterVolumeMin));
            this->slider->setValueChangedListener([this](std::uint8_t val) {
                this->master_volume = static_cast<float>(val) * (MasterVolumeGui::MasterVolumeMax - MasterVolumeGui::MasterVolumeMin) / 100.0f;
                audctlSetSystemOutputMasterVolume(this->master_volume);
            });

            this->reset_button = new tsl::elm::ListItem("Reset");
            this->reset_button->setClickListener([this](std::uint64_t keys) {
                if (keys & HidNpadButton_A) {
                    this->master_volume = MasterVolumeGui::MasterVolumeDefault;
                    this->slider->setProgress(this->master_volume * 100.0f / (MasterVolumeGui::MasterVolumeMax - MasterVolumeGui::MasterVolumeMin));
                    audctlSetSystemOutputMasterVolume(this->master_volume);
                    return true;
                }
                return false;
            });

            list->addItem(this->header);
            list->addItem(this->slider);
            list->addItem(this->reset_button);

            frame->setContent(list);
            return frame;
        }

        virtual void update() override {
            this->header->setText(std::format("Volume: {:.2f}\n", this->master_volume));
        }

    private:
        tsl::elm::CategoryHeader *header;
        tsl::elm::TrackBar       *slider;
        tsl::elm::ListItem       *reset_button;

        float master_volume = 0.0f;
};

class MasterVolumeOverlay: public tsl::Overlay {
    public:
        virtual void initServices() override {
            audctlInitialize();
        }

        virtual void exitServices() override {
            audctlExit();
        }

        virtual void onShow() override {}
        virtual void onHide() override {}

        virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
            return initially<MasterVolumeGui>();
        }
};

int main(int argc, char **argv) {
    return tsl::loop<MasterVolumeOverlay>(argc, argv);
}
