//
//  raylees_tweaks_tab.hpp
//  Moonlight
//

#pragma once

#include <borealis.hpp>

class RayleesTweaksTab : public brls::Box {
  public:
    RayleesTweaksTab();
    ~RayleesTweaksTab();

    static brls::View* create();

  private:
    int hostIndex = 0;
    BRLS_BIND(brls::SelectorCell, linkHost, "link_host");
    BRLS_BIND(brls::DetailCell, linkTest, "link_test");
};
