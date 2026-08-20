#include "Arcanix/Globals.h"
#include "Arcanix/UE4Utils.h"
#include "Arcanix/System.h"
#include "Helper.h"
#include "Arcanix/ESP.h"
#include "Arcanix/aim.h"
#include "Arcanix/UI.h"
#include "Arcanix/Incl.h"

#include <dlfcn.h>
#include <sys/mman.h>
#include <unwind.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "Arcanix/Bangjo.h"

struct {
    int screenWidth, screenHeight;
} egl;

ImVec2 windowPos = ImVec2(50, 50);
ImVec2 touchStart;
bool isDragging = false;

int32_t (*orig_AInputQueue_getEvent)(AInputQueue *queue, AInputEvent **outEvent);

void *protectFunc(void *addr) {
    size_t pageSize = sysconf(_SC_PAGESIZE);
    void *alignedAddr = (void *)((uintptr_t)addr & ~(pageSize - 1));
    mprotect(alignedAddr, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);
    return addr;
}

ImVec2 SmoothMove(ImVec2 start, ImVec2 end, float speed) {
    return ImVec2(start.x + (end.x - start.x) * speed, start.y + (end.y - start.y) * speed);
}

int32_t hooked_AInputQueue_getEvent(AInputQueue *queue, AInputEvent **outEvent) {
    int32_t res = orig_AInputQueue_getEvent(queue, outEvent);
    if (res >= 0 && initImGui) {
        int eventType = AInputEvent_getType(*outEvent);
        if (eventType == AINPUT_EVENT_TYPE_MOTION) { 
            int action = AMotionEvent_getAction(*outEvent) & AMOTION_EVENT_ACTION_MASK;  
            float x = AMotionEvent_getX(*outEvent, 0);  
            float y = AMotionEvent_getY(*outEvent, 0);  

            float normX = (x / (float)egl.screenWidth) * glWidth;
            float normY = (y / (float)egl.screenHeight) * glHeight;  
            
            ImGuiIO& io = ImGui::GetIO();  
            io.MousePos = ImVec2(normX, normY);

            switch (action) {  
                case AMOTION_EVENT_ACTION_DOWN:  
                    io.MouseDown[0] = true;  
                    isDragging = true;
                    touchStart = ImVec2(normX - windowPos.x, normY - windowPos.y);  
                    break;  
                case AMOTION_EVENT_ACTION_MOVE:  
                    if (isDragging) {  
                        ImVec2 targetPos = ImVec2(normX - touchStart.x, normY - touchStart.y);
                        windowPos = SmoothMove(windowPos, targetPos, 0.2f);  
                    }  
                    break;  
                case AMOTION_EVENT_ACTION_UP:  
                    io.MouseDown[0] = false;  
                    isDragging = false;  
                    break;
            }  
            return 1;
        }  
    }  
    return res;
}

EGLBoolean (*orig_arcanixRENDER)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean _arcanixRENDER(EGLDisplay dpy, EGLSurface surface) {
    
    if (!g_GameReady) {
        return orig_arcanixRENDER(dpy, surface);
    }

    eglQuerySurface(dpy, surface, EGL_WIDTH, &glWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &glHeight);
    if (glWidth <= 0 || glHeight <= 0)
        return orig_arcanixRENDER(dpy, surface);

    if (!g_App || !g_App->window)
        return orig_arcanixRENDER(dpy, surface);

    screenWidth = ANativeWindow_getWidth(g_App->window);
    screenHeight = ANativeWindow_getHeight(g_App->window);

    egl.screenWidth = screenWidth;
    egl.screenHeight = screenHeight;
    
    G_Scale = 1.0f;

    if (!initImGui) {
        InitTexture();
        ImGui::CreateContext();
        ImGui_ImplAndroid_Init();
        ImGui_ImplOpenGL3_Init("#version 300 es");

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.IniFilename = NULL;

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0, 0);
        style.WindowRounding = 16.0f * G_Scale;
        style.ScaleAllSizes(G_Scale);

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        
        if (sizeof(Jost) > 100) {
            sssfont = io.Fonts->AddFontFromMemoryTTF((void *)Jost, sizeof(Jost), 18.0f * G_Scale, NULL, io.Fonts->GetGlyphRangesCyrillic());
        } else {
            sssfont = io.Fonts->AddFontDefault(); 
        }

        if (Icons::DataSize > 0) {
            ImFontConfig icon_cfg;
            icon_cfg.FontDataOwnedByAtlas = false;
            icon_cfg.PixelSnapH = true;
            g_IconFont = io.Fonts->AddFontFromMemoryTTF((void*)Icons::RawData, Icons::DataSize, 24.0f * G_Scale, &icon_cfg, io.Fonts->GetGlyphRangesDefault());
        }
        
        Config.ColorsESP.Line = CREATE_COLOR(255, 0, 0, 255);
        Config.ColorsESP.Box = CREATE_COLOR(255, 0, 255, 255);
        initImGui = true;
    }

    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(glWidth, glHeight);
    ImGui::NewFrame();

    if (bValid && g_Auth == g_Token) {
        DrawESP(ImGui::GetBackgroundDrawList());
        DrawAIM(ImGui::GetBackgroundDrawList());
    }
        

    
    RenderUI();
    
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return orig_arcanixRENDER(dpy, surface);
}

int32_t (*orig_ANativeWindow_getWidth)(ANativeWindow *window);
int32_t _ANativeWindow_getWidth(ANativeWindow *window) {
    screenWidth = orig_ANativeWindow_getWidth(window);
    return orig_ANativeWindow_getWidth(window);
}

int32_t (*orig_ANativeWindow_getHeight)(ANativeWindow *window);
int32_t _ANativeWindow_getHeight(ANativeWindow *window) {
    screenHeight = orig_ANativeWindow_getHeight(window);
    return orig_ANativeWindow_getHeight(window);
}




void *main_thread(void *) {
    std::string packageName = "unknown";
    std::ifstream cmdlineFile("/proc/self/cmdline");
    if (cmdlineFile.is_open()) {
        std::getline(cmdlineFile, packageName, '\0'); 
        cmdlineFile.close();
    }

    InitOffsets(packageName);
    LoadCFG();
    LoadKey();

    // ==========================================
    // ШАГ 1: ИНИЦИАЛИЗАЦИЯ ХУКОВ (До загрузки игры)
    // ==========================================
    // Системные библиотеки уже в памяти. Ставим капканы сразу, 
    // чтобы libBangjo.so не успела проскочить.

    // 1.1 Хуки для блокировки Bangjo (OpenGL и AMotionEvent)
    BangjoJammer::InitHooks(); 

    // 1.2 Хук для чтения касаний нашего меню ImGui
    void *libAndroid = dlopen_ex(OBFUSCATE("libandroid.so"), 4);
    if (libAndroid) {
        void *symEvent = dlsym_ex(libAndroid, OBFUSCATE("AInputQueue_getEvent"));
        if (symEvent) {
            protectFunc(symEvent);
            DobbyHook(symEvent, (void *)hooked_AInputQueue_getEvent, (void **)&orig_AInputQueue_getEvent);
        }
        dlclose_ex(libAndroid);
    }
    
    // 1.3 Хук для отрисовки графики (ESP, ImGui)
    void *egl = dlopen_ex("libEGL.so", 4);
    while (!egl) {
        sleep(1);
        egl = dlopen_ex("libEGL.so", 4);
    }
    DobbyHook((void *) dlsym_ex(egl, "eglSwapBuffers"), (void *) _arcanixRENDER, (void **) &orig_arcanixRENDER);

    BangjoJammer::StartThread();

    // ==========================================
    // ШАГ 3: ОЖИДАНИЕ ИГРОВОГО ДВИЖКА (UE4)
    // ==========================================
    UE4 = Tools::GetBaseAddress("libUE4.so");
    while (!UE4) {
        sleep(2);
        UE4 = Tools::GetBaseAddress("libUE4.so");
    }

    while (!g_App) {
        uintptr_t target_address = UE4 + GNativeAndroidApp_Offset;
        if (target_address > 0x10000000) { 
            g_App = *(android_app **)target_address;
        }
        sleep(2);
    }

    while (!g_App->window) {
        sleep(2); 
    }

    FName::GNames = GetGNames();
    while (!FName::GNames) {
        FName::GNames = GetGNames();
        sleep(2);
    }
        
    UObject::GUObjectArray = (FUObjectArray *)(UE4 + GUObject_Offset);

    // ==========================================
    // ШАГ 4: ОЖИДАНИЕ МИРА И ЗАВЕРШЕНИЕ
    // ==========================================
    while (!GetWorld()) {
        sleep(2);
    }
    
    g_GameReady = true; 
    items_data = json::parse(JSON_ITEMS);
    
    return nullptr;
}




__attribute__((constructor)) void _init() {
    pthread_t t;
    pthread_create(&t, 0, main_thread, 0);
}
