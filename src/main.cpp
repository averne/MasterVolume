#include <cstring>
#include <string>
#include <format>
#include <memory>

#include <switch.h>

#define TESLA_INIT_IMPL
#include <tesla.hpp>

class MasterVolumeGui: public tsl::Gui {
    public:
        constexpr static float MasterVolumeMin     = 0.125f;
        constexpr static float MasterVolumeMax     = 8.0f;
        constexpr static float MasterVolumeDefault = 1.0f;
        constexpr static float MasterVolumeExp     = 3.0f;
        constexpr static auto  SinkSwitchTimeout   = 500ms;

        constexpr static auto ConfigDirPath  = "/config/MasterVolume";
        constexpr static auto ConfigFilePath = "/config/MasterVolume/config.bin";

    public:
        MasterVolumeGui() {
            bool has_vol = false;
            tsl::hlp::doWithSDCardHandle([this, &has_vol] {
                auto *fs = fsdevGetDeviceFileSystem("sdmc");
                if (!fs)
                    return;

                FsFile fp;
                if (auto rc = fsFsOpenFile(fs, MasterVolumeGui::ConfigFilePath, FsOpenMode_Read, &fp); R_FAILED(rc))
                    return;
                tsl::hlp::ScopeGuard guard([&fp] { fsFileClose(&fp); });

                float vol;
                u64 read;
                if (auto rc = fsFileRead(&fp, 0, &vol, sizeof(vol), FsReadOption_None, &read); R_FAILED(rc) || read != sizeof(vol))
                    return;

                this->master_volume = vol, has_vol = true;
            });

            if (has_vol)
                audctlSetSystemOutputMasterVolume(this->master_volume);
            else
                audctlGetSystemOutputMasterVolume(&this->master_volume);
        }

        virtual ~MasterVolumeGui() override {
            tsl::hlp::doWithSDCardHandle([this] {
                auto *fs = fsdevGetDeviceFileSystem("sdmc");
                if (!fs)
                    return;

                if (auto rc = fsFsCreateDirectory(fs, MasterVolumeGui::ConfigDirPath); R_FAILED(rc) && R_DESCRIPTION(rc) != 2)
                    return;

                if (auto rc = fsFsCreateFile(fs, MasterVolumeGui::ConfigFilePath, sizeof(this->master_volume), 0); R_FAILED(rc) && R_DESCRIPTION(rc) != 2)
                    return;

                FsFile fp;
                if (auto rc = fsFsOpenFile(fs, MasterVolumeGui::ConfigFilePath, FsOpenMode_Write, &fp); R_FAILED(rc))
                    return;
                tsl::hlp::ScopeGuard guard([&fp] { fsFileClose(&fp); });

                fsFileWrite(&fp, 0, &this->master_volume, sizeof(this->master_volume), FsWriteOption_None);
            });
        }

        virtual tsl::elm::Element* createUI() override {
            auto *frame = new tsl::elm::OverlayFrame(APP_TITLE, APP_VERSION);

            auto *list = new tsl::elm::List();

            this->mvol_header = new tsl::elm::CategoryHeader("Master volume (max. 8)");
            this->mvol_slider = new tsl::elm::TrackBar("");

            this->mvol_slider->setProgress(this->vol_to_pos(this->master_volume));
            this->mvol_slider->setValueChangedListener([this](std::uint8_t val) {
                this->master_volume = this->pos_to_vol(val);
                audctlSetSystemOutputMasterVolume(this->master_volume);
            });

            this->mvol_reset_button = new tsl::elm::ListItem("Reset");
            this->mvol_reset_button->setClickListener([this](std::uint64_t keys) {
                if (keys & HidNpadButton_A) {
                    this->master_volume = MasterVolumeGui::MasterVolumeDefault;
                    this->mvol_slider->setProgress(this->vol_to_pos(this->master_volume));
                    audctlSetSystemOutputMasterVolume(this->master_volume);
                    return true;
                }
                return false;
            });

            this->sink_header = new tsl::elm::CategoryHeader("Audio sink");

            this->sink_speakers_button = new tsl::elm::ListItem("Force speaker output");
            this->sink_speakers_button->setClickListener([this](std::uint64_t keys) {
                if (keys & HidNpadButton_A) {
                    constexpr auto timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(MasterVolumeGui::SinkSwitchTimeout).count();
                    audctlSetDefaultTarget(AudioTarget_Speaker, timeout, timeout);
                    return true;
                }
                return false;
            });

            list->addItem(this->mvol_header);
            list->addItem(this->mvol_slider);
            list->addItem(this->mvol_reset_button);
            list->addItem(this->sink_header);
            list->addItem(this->sink_speakers_button);

            frame->setContent(list);
            return frame;
        }

        virtual void update() override {
            this->mvol_header->setText(std::format("Volume: {:.2f}\n", this->master_volume));
        }

    private:
        constexpr float pos_to_vol(std::uint8_t pos) {
            constexpr float delta = MasterVolumeGui::MasterVolumeMax - MasterVolumeGui::MasterVolumeMin;
            constexpr float exp   = 1.0f/MasterVolumeGui::MasterVolumeExp;
            constexpr float mult  = std::pow(delta, exp) / 100.0f;

            return std::pow(static_cast<float>(pos) * mult, MasterVolumeGui::MasterVolumeExp)
                + MasterVolumeGui::MasterVolumeMin;
        };

        constexpr std::uint8_t vol_to_pos(float vol) {
            constexpr float delta = MasterVolumeGui::MasterVolumeMax - MasterVolumeGui::MasterVolumeMin;
            constexpr float exp   = 1.0f/MasterVolumeGui::MasterVolumeExp;
            constexpr float mult  = 100.0f / std::pow(delta, exp);

            return std::pow(vol - MasterVolumeGui::MasterVolumeMin, exp) * mult;
        };

    private:
        tsl::elm::CategoryHeader *mvol_header;
        tsl::elm::TrackBar       *mvol_slider;
        tsl::elm::ListItem       *mvol_reset_button;
        tsl::elm::CategoryHeader *sink_header;
        tsl::elm::ListItem       *sink_speakers_button;

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
