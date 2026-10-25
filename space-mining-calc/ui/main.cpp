#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <print>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

import core;

namespace {

// Desktop asks for OpenGL 3.0 core; the browser gets WebGL 2 (OpenGL ES 3.0).
#ifdef __EMSCRIPTEN__
constexpr auto kGlslVersion = "#version 300 es";
constexpr int kGlProfile = SDL_GL_CONTEXT_PROFILE_ES;
#else
constexpr auto kGlslVersion = "#version 130";
constexpr int kGlProfile = SDL_GL_CONTEXT_PROFILE_CORE;
#endif

struct App {
  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  bool done = false;
  bool showDemo = false;
};

auto init(App& app) -> bool {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::println(stderr, "SDL_Init: {}", SDL_GetError());
    return false;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, kGlProfile);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  app.window = SDL_CreateWindow(
      "Space mining calculator", static_cast<int>(1280 * scale),
      static_cast<int>(800 * scale),
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
          SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (app.window == nullptr) {
    std::println(stderr, "SDL_CreateWindow: {}", SDL_GetError());
    return false;
  }
  app.gl = SDL_GL_CreateContext(app.window);
  if (app.gl == nullptr) {
    std::println(stderr, "SDL_GL_CreateContext: {}", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(app.window, app.gl);
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef __EMSCRIPTEN__
  // No imgui.ini in the browser: there is no file system to keep it.
  io.IniFilename = nullptr;
#endif
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImGui::GetStyle().FontScaleDpi = scale;

  ImGui_ImplSDL3_InitForOpenGL(app.window, app.gl);
  ImGui_ImplOpenGL3_Init(kGlslVersion);
  return true;
}

auto drawUi(App& app) -> void {
  ImGui::Begin("Space mining");
  ImGui::Text("%.*s", static_cast<int>(core::kName.size()),
              core::kName.data());
  ImGui::Checkbox("ImGui demo", &app.showDemo);
  ImGui::End();
  if (app.showDemo) ImGui::ShowDemoWindow(&app.showDemo);
}

auto frame(App& app) -> void {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) app.done = true;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  drawUi(app);
  ImGui::Render();

  const ImGuiIO& io = ImGui::GetIO();
  glViewport(0, 0, static_cast<int>(io.DisplaySize.x),
             static_cast<int>(io.DisplaySize.y));
  glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(app.window);
}

#ifndef __EMSCRIPTEN__
// The browser never returns from its loop, so nothing to tear down there.
auto shutdown(App& app) -> void {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DestroyContext(app.gl);
  SDL_DestroyWindow(app.window);
  SDL_Quit();
}
#endif

}  // namespace

auto main() -> int {
  static App app;  // outlives main() in the browser, see below
  if (!init(app)) return 1;
#ifdef __EMSCRIPTEN__
  // The browser owns the loop: frame() runs once per animation frame, and
  // main() returns without unwinding, so `app` must not live on its stack.
  emscripten_set_main_loop_arg(
      [](void* arg) { frame(*static_cast<App*>(arg)); }, &app, 0, true);
#else
  while (!app.done) frame(app);
  shutdown(app);
#endif
  return 0;
}
