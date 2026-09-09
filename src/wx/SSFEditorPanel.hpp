#pragma once
#include "NeoModulePanel.hpp"
#include "../core/AppModel.hpp"
#include <neoshared/ResourceDocument.hpp>
namespace neossf::ui {
inline constexpr unsigned kEditorApiVersion=1;
class SSFEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path& path)=0;
    virtual bool openResource(neoshared::ResourceDocument resource)=0;
    virtual bool saveActiveAs(const std::filesystem::path& path)=0;
    virtual bool activateResource(const std::string& identity)=0;
    virtual std::size_t documentCount() const=0;
    virtual AppModel* activeModel()=0;
    virtual void refreshActiveModel()=0;
};
SSFEditorPanel* createEditorPanel(wxWindow* parent, neomodules::Context context={});
}
