//
//  raylees_tweaks_tab.cpp
//  Moonlight
//

#include "raylees_tweaks_tab.hpp"
#include "Settings.hpp"
#include "link_test_view.hpp"

#include <algorithm>

using namespace brls;
using namespace brls::literals;

RayleesTweaksTab::RayleesTweaksTab() {
    this->inflateFromXMLRes("xml/tabs/raylees_tweaks.xml");

    frameQueue->setText("settings/frame_buffer"_i18n);
    frameQueue->setData({"1", "2", "3", "4"});
    const int queued = Settings::instance().frames_queue_size();
    frameQueue->setSelection(queued >= 1 && queued <= 4 ? queued - 1 : 0);
    frameQueue->getEvent()->subscribe([](int selected) {
        Settings::instance().set_frames_queue_size(selected + 1);
        Settings::instance().save();
    });

    const auto hosts = Settings::instance().hosts();
    linkTest->setText("Link test");
    if (hosts.empty()) {
        linkHost->setVisibility(Visibility::GONE);
        linkTest->setDetailText("Add a host first");
        return;
    }

    std::vector<std::string> names;
    names.reserve(hosts.size());
    for (const Host& host : hosts)
        names.push_back(host.hostname.empty() ? host.preferred_address() : host.hostname);
    linkHost->setText("Host");
    linkHost->setData(names);
    linkHost->setSelection(0);
    linkHost->getEvent()->subscribe([this](int selected) { hostIndex = selected; });
    linkTest->setDetailText("Picture and graphs");
    linkTest->registerClickAction([this](View*) {
        const auto currentHosts = Settings::instance().hosts();
        if (currentHosts.empty())
            return true;
        const int index = std::clamp(hostIndex, 0, static_cast<int>(currentHosts.size()) - 1);
        auto* view = new LinkTestView(currentHosts[index]);
        view->getAppletFrameItem()->title = "Link test";
        auto* frame = new AppletFrame(view);
        frame->setHeaderVisibility(Visibility::GONE);
        frame->setFooterVisibility(Visibility::GONE);
        frame->setBackground(ViewBackground::NONE);
        Application::pushActivity(new Activity(frame));
        return true;
    });
}

RayleesTweaksTab::~RayleesTweaksTab() { Settings::instance().save(); }

brls::View* RayleesTweaksTab::create() { return new RayleesTweaksTab(); }
