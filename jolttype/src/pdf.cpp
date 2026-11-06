#include "jolttype/publishing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#ifdef JFX_JOLTTYPE_PDF
#include <hpdf.h>
#include <podofo/podofo.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include "fpdfview.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace jolttype {
namespace {
void fail(Diagnostic &e,const char *s) {e={};std::snprintf(e.message,sizeof(e.message),"%s",s);}
#ifdef JFX_JOLTTYPE_PDF
constexpr size_t max_pdf_bytes=64*1024*1024;
struct Pdfium {
    std::mutex mutex;
    void *library=nullptr;
    bool initialized=false;
#define JT_PDF_SYMBOLS(X) \
    X(FPDF_InitLibraryWithConfig) X(FPDF_DestroyLibrary) X(FPDF_LoadMemDocument64) \
    X(FPDF_CloseDocument) X(FPDF_GetPageCount) X(FPDF_LoadPage) X(FPDF_ClosePage) \
    X(FPDF_GetPageWidth) X(FPDF_GetPageHeight) X(FPDFBitmap_CreateEx) \
    X(FPDFBitmap_Destroy) X(FPDFBitmap_FillRect) X(FPDF_RenderPageBitmap)
#define FIELD(name) decltype(&name) name##_fn=nullptr;
    JT_PDF_SYMBOLS(FIELD)
#undef FIELD
    template<class T> bool symbol(const char *name,T &out) {
#ifdef _WIN32
        auto p=GetProcAddress(static_cast<HMODULE>(library),name);
#else
        auto p=dlsym(library,name);
#endif
        static_assert(sizeof(p)==sizeof(out)); std::memcpy(&out,&p,sizeof(out)); return out!=nullptr;
    }
    Pdfium() {
        const char *path=std::getenv("JOLTTYPE_PDFIUM_LIBRARY");
        if(!path || !*path)path=JFX_JOLTTYPE_PDFIUM_LIBRARY;
        if(!*path) {
#ifdef _WIN32
            path="pdfium.dll";
#elif defined(__APPLE__)
            path="libpdfium.dylib";
#else
            path="libpdfium.so";
#endif
        }
#ifdef _WIN32
        library=LoadLibraryA(path);
#else
        library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
        if(!library)return;
#define LOAD(name) if(!symbol(#name,name##_fn))return;
        JT_PDF_SYMBOLS(LOAD)
#undef LOAD
        FPDF_LIBRARY_CONFIG config{};config.version=2;FPDF_InitLibraryWithConfig_fn(&config);initialized=true;
    }
    ~Pdfium() {
        if(initialized)FPDF_DestroyLibrary_fn();
        if(library) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(library));
#else
            dlclose(library);
#endif
        }
    }
};
Pdfium &pdfium() {static Pdfium api;return api;}
void open_qpdf(QPDF &pdf,std::string_view bytes) {
    if(bytes.empty() || bytes.size()>max_pdf_bytes)throw std::runtime_error("PDF exceeds the 64 MiB limit or is empty.");
    pdf.setSuppressWarnings(true);pdf.setAttemptRecovery(false);
    pdf.processMemoryFile("JoltType",bytes.data(),bytes.size());
}
void finish_pdf(std::string_view buffer,size_t pages,tilly::string &out,Progress progress,void *user) {
    if(progress)progress(user,80,"qpdf: validating and optimizing PDF");
    QPDF checked;open_qpdf(checked,buffer);
    if(QPDFPageDocumentHelper(checked).getAllPages().size()!=pages)throw std::runtime_error("PDF page count does not match the document.");
    QPDFWriter writer(checked);writer.setOutputMemory();writer.setDeterministicID(true);writer.write();
    auto bytes=writer.getBufferSharedPointer();
    if(bytes->getSize()>max_pdf_bytes)throw std::runtime_error("Generated PDF exceeds 64 MiB.");
    tilly::string candidate(reinterpret_cast<const char *>(bytes->getBuffer()),bytes->getSize());out.swap(candidate);
}
void *HPDF_STDCALL haru_alloc(HPDF_UINT size) {
    return tilly_alloc(const_cast<tilly_allocator_t *>(tilly_default_allocator()),size,alignof(max_align_t));
}
void HPDF_STDCALL haru_free(void *ptr) {
    tilly_free(const_cast<tilly_allocator_t *>(tilly_default_allocator()),ptr);
}
void haru_check(HPDF_STATUS status) {
    if(status==HPDF_OK)return;
    char message[128];std::snprintf(message,sizeof(message),"libharu PDF generation failed (0x%04lx).",static_cast<unsigned long>(status));
    throw std::runtime_error(message);
}
template<class T> T haru_object(T object,HPDF_Doc pdf) {
    if(!object){haru_check(HPDF_GetError(pdf));throw std::runtime_error("libharu could not create a PDF object.");}
    return object;
}
#endif
}
bool pdf_available() noexcept {
#ifdef JFX_JOLTTYPE_PDF
    return true;
#else
    return false;
#endif
}
bool pdf_preview_available() noexcept {
#ifdef JFX_JOLTTYPE_PDF
    return pdfium().initialized;
#else
    return false;
#endif
}
bool publish_pdf(const Document &doc,tilly::string &out,Diagnostic &error,Progress progress,void *user) noexcept {
#ifdef JFX_JOLTTYPE_PDF
    try {
        // Share the existing system-font resolver, but all PDF objects, drawing
        // and serialization in the primary exporter are owned by libharu.
        PoDoFo::PdfMemDocument font_source;
        struct Owner {HPDF_Doc pdf;~Owner(){if(pdf)HPDF_Free(pdf);}} owner{HPDF_NewEx(nullptr,haru_alloc,haru_free,0,nullptr)};
        auto pdf=owner.pdf;
        if(!pdf)throw std::runtime_error("Not enough memory to create the libharu PDF document.");
        haru_check(HPDF_SetCompressionMode(pdf,HPDF_COMP_ALL));
        haru_check(HPDF_SetInfoAttr(pdf,HPDF_INFO_CREATOR,"JoltType / libharu"));
        HPDF_Font font=nullptr;
        for(size_t i=0;i<doc.pages.size();++i) {
            const auto &p=doc.pages[i];auto page=haru_object(HPDF_AddPage(pdf),pdf);
            haru_check(HPDF_Page_SetWidth(page,HPDF_REAL(p.width)));
            haru_check(HPDF_Page_SetHeight(page,HPDF_REAL(p.height)));
            for(const auto &e:doc.elements) if(e.page==i) {
                haru_check(HPDF_Page_SetRGBFill(page,HPDF_REAL((e.color>>16)&255)/255,HPDF_REAL((e.color>>8)&255)/255,HPDF_REAL(e.color&255)/255));
                if(e.kind==Kind::Rectangle) {
                    haru_check(HPDF_Page_Rectangle(page,HPDF_REAL(e.x),HPDF_REAL(p.height-e.y-e.height),HPDF_REAL(e.width),HPDF_REAL(e.height)));
                    haru_check(HPDF_Page_Fill(page));
                } else if(e.kind==Kind::Image) {
                    auto *data=reinterpret_cast<const HPDF_BYTE *>(e.image.data());
                    auto image=haru_object(e.image.size()>1 && data[0]==0x89?
                        HPDF_LoadPngImageFromMem(pdf,data,HPDF_UINT(e.image.size())):
                        HPDF_LoadJpegImageFromMem(pdf,data,HPDF_UINT(e.image.size())),pdf);
                    haru_check(HPDF_Page_DrawImage(page,image,HPDF_REAL(e.x),HPDF_REAL(p.height-e.y-e.height),HPDF_REAL(e.width),HPDF_REAL(e.height)));
                } else {
                    if(!font) {
                        auto *resolved=font_source.GetFonts().SearchFont("monospace");
                        if(!resolved)throw std::runtime_error("No monospace font found for PDF output.");
                        const auto data=resolved->GetMetrics().GetOrLoadFontFileData();
                        if(data.empty() || data.size()>max_pdf_bytes)throw std::runtime_error("Invalid font data for libharu PDF output.");
                        auto name=haru_object(HPDF_LoadTTFontFromMemory(pdf,reinterpret_cast<const HPDF_BYTE *>(data.data()),HPDF_UINT(data.size()),HPDF_TRUE),pdf);
                        haru_check(HPDF_UseUTFEncodings(pdf));font=haru_object(HPDF_GetFont(pdf,name,"UTF-8"),pdf);
                    }
                    haru_check(HPDF_Page_BeginText(page));
                    haru_check(HPDF_Page_SetFontAndSize(page,font,HPDF_REAL(e.size)));
                    double y=p.height-e.y-e.size;
                    for(const auto &line:e.lines) {
                        double x=e.x;
                        for(size_t c=0;c<line.text.size();) {
                            if(static_cast<unsigned char>(line.text[c])>=0xf0)
                                throw std::runtime_error("libharu supports BMP Unicode text; choose PDF (PoDoFo alternative) for supplementary characters.");
                            size_t end=c+1;while(end<line.text.size() && (static_cast<unsigned char>(line.text[end])&0xc0)==0x80)++end;
                            char scalar[5]{};
                            if(end-c>4)throw std::runtime_error("Invalid UTF-8 in PDF text.");
                            std::memcpy(scalar,line.text.data()+c,end-c);
                            haru_check(HPDF_Page_TextOut(page,HPDF_REAL(x),HPDF_REAL(y),scalar));
                            x+=e.size*.6;c=end;
                        }
                        y-=e.size*1.2;
                    }
                    haru_check(HPDF_Page_EndText(page));
                }
            }
            if(progress)progress(user,unsigned(70*(i+1)/doc.pages.size()),"libharu: typesetting final PDF");
        }
        haru_check(HPDF_SaveToStream(pdf));auto size=HPDF_GetStreamSize(pdf);
        if(!size || size>max_pdf_bytes)throw std::runtime_error("Generated PDF is empty or exceeds 64 MiB.");
        tilly::string buffer(size,'\0');haru_check(HPDF_ResetStream(pdf));
        auto status=HPDF_ReadFromStream(pdf,reinterpret_cast<HPDF_BYTE *>(buffer.data()),&size);
        if(status!=HPDF_STREAM_EOF)haru_check(status);
        if(size!=buffer.size())throw std::runtime_error("Could not read the complete libharu PDF.");
        finish_pdf(buffer,doc.pages.size(),out,progress,user);
        error={};if(progress)progress(user,100,"PDF complete");return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
#else
    (void)doc;(void)out;(void)progress;(void)user;fail(error,"PDF publishing requires JFX_JOLTTYPE_PDF.");return false;
#endif
}
bool publish_pdf_podofo(const Document &doc,tilly::string &out,Diagnostic &error,Progress progress,void *user) noexcept {
#ifdef JFX_JOLTTYPE_PDF
    try {
        using namespace PoDoFo;
        PdfMemDocument pdf;
        auto *font=pdf.GetFonts().SearchFont("monospace");
        if(!font)throw std::runtime_error("No monospace font found for PDF output.");
        for(size_t i=0;i<doc.pages.size();++i) {
            const auto &p=doc.pages[i];auto &page=pdf.GetPages().CreatePage(Rect(0,0,p.width,p.height));
            PdfPainter painter;painter.SetCanvas(page);
            try {
                for(const auto &e:doc.elements) if(e.page==i) {
                    painter.GraphicsState.SetNonStrokingColor(PdfColor(double((e.color>>16)&255)/255,double((e.color>>8)&255)/255,double(e.color&255)/255));
                    if(e.kind==Kind::Rectangle)painter.DrawRectangle(e.x,p.height-e.y-e.height,e.width,e.height,PdfPathDrawMode::Fill);
                    else if(e.kind==Kind::Image) {
                        auto image=pdf.CreateImage();image->LoadFromBuffer(bufferview(e.image.data(),e.image.size()));
                        painter.DrawImage(*image,e.x,p.height-e.y-e.height,e.width/double(e.image_width),e.height/double(e.image_height));
                    }
                    else {
                        painter.TextState.SetFont(*font,e.size);double y=p.height-e.y-e.size;
                        for(const auto &line:e.lines) {
                            double x=e.x;
                            for(size_t c=0;c<line.text.size();) {
                                size_t end=c+1;while(end<line.text.size() && (static_cast<unsigned char>(line.text[end])&0xc0)==0x80)++end;
                                painter.DrawText(std::string_view(line.text.data()+c,end-c),x,y);x+=e.size*.6;c=end;
                            }
                            y-=e.size*1.2;
                        }
                    }
                }
                painter.FinishDrawing();
            } catch(...) {try {painter.FinishDrawing();}catch(...){} throw;}
            if(progress)progress(user,unsigned(70*(i+1)/doc.pages.size()),"PoDoFo: typesetting pages");
        }
        pdf.GetMetadata().SetCreator(PdfString("JoltType / PoDoFo"));
        tilly::string buffer;ContainerStreamDevice<tilly::string> device(buffer);pdf.Save(device);
        finish_pdf(buffer,doc.pages.size(),out,progress,user);
        error={};if(progress)progress(user,100,"PDF complete");return true;
    } catch(const std::exception &e) {fail(error,e.what());return false;}
#else
    (void)doc;(void)out;(void)progress;(void)user;fail(error,"PDF publishing requires JFX_JOLTTYPE_PDF.");return false;
#endif
}
bool inspect_pdf(std::string_view bytes,size_t &pages,Diagnostic &error) noexcept {
#ifdef JFX_JOLTTYPE_PDF
    try {
        QPDF pdf;open_qpdf(pdf,bytes);size_t n=QPDFPageDocumentHelper(pdf).getAllPages().size();
        if(!n || n>max_pages)throw std::runtime_error("PDF page count exceeds the JoltType limit.");
        pages=n;error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
#else
    (void)bytes;(void)pages;fail(error,"PDF inspection requires JFX_JOLTTYPE_PDF.");return false;
#endif
}
bool render_pdf(std::string_view bytes,size_t page,unsigned dimension,Bitmap &out,Diagnostic &error) noexcept {
#ifdef JFX_JOLTTYPE_PDF
    try {
        if(dimension<64 || dimension>2048)throw std::runtime_error("PDF preview size must be 64..2048 pixels.");
        size_t count=0;if(!inspect_pdf(bytes,count,error))return false;
        if(page>=count)throw std::runtime_error("PDF page index is out of range.");
        auto &api=pdfium();std::lock_guard<std::mutex> lock(api.mutex);
        if(!api.initialized)throw std::runtime_error("PDFium unavailable. Set JOLTTYPE_PDFIUM_LIBRARY to the PDFium runtime library.");
        struct Doc {Pdfium &a;FPDF_DOCUMENT d;~Doc(){if(d)a.FPDF_CloseDocument_fn(d);}} doc{api,api.FPDF_LoadMemDocument64_fn(bytes.data(),bytes.size(),nullptr)};
        if(!doc.d)throw std::runtime_error("PDFium could not load the generated PDF.");
        struct PageOwner {Pdfium &a;FPDF_PAGE p;~PageOwner(){if(p)a.FPDF_ClosePage_fn(p);}} p{api,api.FPDF_LoadPage_fn(doc.d,int(page))};
        if(!p.p)throw std::runtime_error("PDFium could not load the selected page.");
        double width=api.FPDF_GetPageWidth_fn(p.p),height=api.FPDF_GetPageHeight_fn(p.p);
        if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0)throw std::runtime_error("Invalid PDF page size.");
        double scale=dimension/std::max(width,height);Bitmap next;
        next.width=std::max(1u,unsigned(std::lround(width*scale)));next.height=std::max(1u,unsigned(std::lround(height*scale)));
        next.rgba.resize(size_t(next.width)*next.height*4);
        struct Image {Pdfium &a;FPDF_BITMAP b;~Image(){if(b)a.FPDFBitmap_Destroy_fn(b);}} image{api,api.FPDFBitmap_CreateEx_fn(int(next.width),int(next.height),FPDFBitmap_BGRA,next.rgba.data(),int(next.width*4))};
        if(!image.b)throw std::runtime_error("PDFium bitmap allocation failed.");
        api.FPDFBitmap_FillRect_fn(image.b,0,0,int(next.width),int(next.height),0xffffffff);
        api.FPDF_RenderPageBitmap_fn(image.b,p.p,0,0,int(next.width),int(next.height),0,FPDF_ANNOT);
        for(size_t i=0;i<next.rgba.size();i+=4)std::swap(next.rgba[i],next.rgba[i+2]);
        out=std::move(next);error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
#else
    (void)bytes;(void)page;(void)dimension;(void)out;fail(error,"PDF preview requires JFX_JOLTTYPE_PDF.");return false;
#endif
}
}
