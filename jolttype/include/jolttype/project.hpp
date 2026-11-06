#ifndef JOLTTYPE_PROJECT_HPP
#define JOLTTYPE_PROJECT_HPP
#include "publishing.hpp"
namespace jolttype {
constexpr size_t max_asset_bytes=16*1024*1024, max_inventory_bytes=64*1024*1024, max_assets=128;
enum class AssetKind { Image, Text, Code, Source, Compiled, Module, Other };
struct Asset {
    tilly::string id, path;
    AssetKind kind=AssetKind::Other;
    tilly::vector<tilly::string> tags, dependencies;
    tilly::string bytes;
};
class Inventory {
public:
    const tilly::vector<Asset> &assets() const {return assets_;}
    const Asset *find(std::string_view id) const;
    bool put(Asset asset,Diagnostic &) noexcept;
    bool remove(std::string_view id,Diagnostic &) noexcept;
    bool tag(std::string_view id,tilly::vector<tilly::string> tags,Diagnostic &) noexcept;
    bool validate(Diagnostic &) const noexcept;
    bool replace(tilly::vector<Asset>,Diagnostic &) noexcept;
    bool import_file(const char *path,std::string_view id,AssetKind kind,
        tilly::vector<tilly::string> tags,Diagnostic &) noexcept;
private:
    tilly::vector<Asset> assets_;
};
struct BuildStep {tilly::string input,output,target;};
struct Interface {
    tilly::string id,module,entry,label;
    tilly::vector<tilly::string> parameters;
};
struct Manifest {
    tilly::string name="untitled",version="1.0.0",author,description,entry="main",kind="document";
    tilly::vector<BuildStep> build;
    tilly::vector<Interface> interfaces;
};
struct Typelet {Manifest manifest;Inventory inventory;};
bool parse_manifest(std::string_view,Manifest &,tilly::vector<Asset> &descriptors,Diagnostic &) noexcept;
bool manifest_text(const Typelet &,tilly::string &,Diagnostic &) noexcept;
bool pack_typelet(const Typelet &,tilly::string &,Diagnostic &) noexcept;
bool unpack_typelet(std::string_view,Typelet &,Diagnostic &) noexcept;
bool build_typelet(Typelet &,Diagnostic &,Progress=nullptr,void *user=nullptr) noexcept;
bool compile_object(const Document &,tilly::string &,Diagnostic &) noexcept;
bool load_object(std::string_view,Document &,Diagnostic &) noexcept;
// Modules are scalar expression libraries: \module{name}, followed by
// \export{name}{comma-separated parameters}{expression}. Output is real WASM.
bool compile_module(std::string_view,tilly::string &wasm,Diagnostic &) noexcept;
bool modules_available() noexcept;
class Module {
public:
    Module()=default;
    ~Module();
    Module(const Module &)=delete;Module &operator=(const Module &)=delete;
    bool load(std::string_view wasm,Diagnostic &) noexcept;
    bool call(const char *name,const double *args,size_t count,double &result,Diagnostic &) noexcept;
private:
    void *runtime_=nullptr;
};
// Hatch keeps a dedicated checkpoint, independent of committed history.
class Hatch {
public:
    bool begin(const Session &,Diagnostic &) noexcept;
    bool restore(Session &,double now,Diagnostic &) noexcept;
    bool active() const {return active_;}
    void accept() {source_.clear();active_=false;}
private:
    tilly::string source_; bool active_=false;
};
}
#endif
