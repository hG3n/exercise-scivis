// -----------------------------------------------------------------------------
// Copyright  : (C) 2014 Andreas-C. Bernstein
// License    : MIT (see the file LICENSE)
// Maintainer : Andreas-C. Bernstein <andreas.bernstein@uni-weimar.de>
// Stability  : experimental
//
// Fensterchen Example
// -----------------------------------------------------------------------------
#define _USE_MATH_DEFINES
#include "fensterchen.hpp"
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cmath>

///GLM INCLUDES
#define GLM_FORCE_RADIANS
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/norm.hpp>

///PROJECT INCLUDES
#include <volume_loader_raw.hpp>
#include <transfer_function.hpp>
#include <utils.hpp>
#include <turntable.hpp>

const std::string g_file_vertex_shader("../../../source/shader/volume.vert");
const std::string g_file_fragment_shader("../../../source/shader/volume.frag");

GLuint loadShaders(std::string const& vs, std::string const& fs)
{
  std::string v = readFile(vs);
  std::string f = readFile(fs);
  return createProgram(v,f);
}

bool g_reload_shader_pressed                = false;
bool g_show_transfer_function               = false;
bool g_show_transfer_function_pressed       = false;
Turntable  g_turntable;

///SETUP VOLUME RAYCASTER HERE
// set the volume file
std::string g_file_string = "../../../data/head_w256_h256_d225_c1_b8.raw";

// set the sampling distance for the ray traversal
float       g_sampling_distance             = 0.001f;

float       g_iso_value                     = 0.2f;

// set the light position and color for shading
glm::vec3   g_light_pos                     = glm::vec3(1.0,  1.0,  1.0);
glm::vec3   g_light_color                   = glm::vec3(1.0f, 1.0f, 1.0f);

// set backgorund color here
//glm::vec3   g_background_color = glm::vec3(1.0f, 1.0f, 1.0f); //white
glm::vec3   g_background_color = glm::vec3(0.0f, 0.0f, 0.0f);   //black

glm::ivec2  g_window_res                    = glm::ivec2(600, 600);

struct Manipulator
{
  Manipulator()
    : m_turntable()
    , m_mouse_button_pressed(0,0,0)
    , m_mouse(0.0f,0.0f)
    , m_lastMouse(0.0f,0.0f)
    {}

  glm::mat4 matrix(Window const& win)
  {
    m_mouse = win.mousePosition();
    if (win.isButtonPressed(Window::MOUSE_BUTTON_LEFT)) {
      if (!m_mouse_button_pressed[0]) {
        m_mouse_button_pressed[0] = 1;
      }
      m_turntable.rotate(m_lastMouse, m_mouse);
      m_slideMouse = m_mouse;
      m_slidelastMouse = m_lastMouse;
    } else {
      m_mouse_button_pressed[0] = 0;
      m_turntable.rotate(m_slidelastMouse, m_slideMouse);
      //m_slideMouse *= 0.99f;
      //m_slidelastMouse *= 0.99f;
    }

    if (win.isButtonPressed(Window::MOUSE_BUTTON_MIDDLE)) {
      if (!m_mouse_button_pressed[1]) {
        m_mouse_button_pressed[1] = 1;
      }
      m_turntable.pan(m_lastMouse, m_mouse);
    } else {
      m_mouse_button_pressed[1] = 0;
    }

    if (win.isButtonPressed(Window::MOUSE_BUTTON_RIGHT)) {
      if (!m_mouse_button_pressed[2]) {
        m_mouse_button_pressed[2] = 1;
      }
      m_turntable.zoom(m_lastMouse, m_mouse);
    } else {
      m_mouse_button_pressed[2] = 0;
    }

    m_lastMouse = m_mouse;    
    return m_turntable.matrix();
  }

private:
  Turntable  m_turntable;
  glm::ivec3 m_mouse_button_pressed;
  glm::vec2  m_mouse;
  glm::vec2  m_lastMouse;
  glm::vec2  m_slideMouse;
  glm::vec2  m_slidelastMouse;
};

int main(int argc, char* argv[])
{  
   Window win(g_window_res);

  // initialize the transfer function
  Transfer_function transfer_fun;
  
  // first clear possible old values
  transfer_fun.reset();

  // the add_stop method takes:
  //  - unsigned char or float - data value     (0.0 .. 1.0) or (0..255)
  //  - vec4f         - color and alpha value   (0.0 .. 1.0) per channel
  transfer_fun.add(0.0f, glm::vec4(0.0, 0.0, 0.0, 0.0));
  transfer_fun.add(1.0f, glm::vec4(1.0, 1.0, 1.0, 1.0));
   

  ///NOTHING TODO UNTIL HERE-------------------------------------------------------------------------------
  
  //init volume loader
  Volume_loader_raw loader;
  //read volume dimensions
  glm::ivec3 vol_dimensions = loader.get_dimensions(g_file_string);

  unsigned max_dim = std::max(std::max(vol_dimensions.x,
                            vol_dimensions.y),
                            vol_dimensions.z);

  // calculating max volume bounds of volume (0.0 .. 1.0)
  glm::vec3 max_volume_bounds = glm::vec3(vol_dimensions) / glm::vec3((float)max_dim);
  
  // loading volume file data
  auto volume_data = loader.load_volume(g_file_string);
  auto channel_size = loader.get_bit_per_channel(g_file_string) / 8;
  auto channel_count = loader.get_channel_count(g_file_string);
  
  // init and upload volume texture
  glActiveTexture(GL_TEXTURE0);
  createTexture3D(vol_dimensions.x, vol_dimensions.y, vol_dimensions.z, channel_size, channel_count, (char*)&volume_data[0]);

  // init and upload transfer function texture
  glActiveTexture(GL_TEXTURE1);
  createTexture2D(255u, 1u, (char*)&transfer_fun.get_RGBA_transfer_function_buffer()[0]);

  // setting up proxy geometry
  Cube cube(glm::vec3(0.0, 0.0, 0.0), max_volume_bounds);

  // loading actual raytracing shader code (volume.vert, volume.frag)
  // edit volume.frag to define the result of our volume raycaster
  GLuint program(0);
  try {
    program = loadShaders(g_file_vertex_shader, g_file_fragment_shader);
  } catch (std::logic_error& e) {
    std::cerr << e.what() << std::endl;
  }

  // init object manipulator (turntable)
  Manipulator manipulator;

  // manage keys here
  // add new input if neccessary (ie changing sampling distance, isovalues, ...)
  while (!win.shouldClose()) {
    // exit window with escape
    if (win.isKeyPressed(GLFW_KEY_ESCAPE)) {
      win.stop();
    }

    if (win.isKeyPressed(GLFW_KEY_LEFT)) {
        g_light_pos.x -= 0.5f;
    }

    if (win.isKeyPressed(GLFW_KEY_RIGHT)) {
        g_light_pos.x += 0.5f;
    }

    if (win.isKeyPressed(GLFW_KEY_UP)) {
        g_light_pos.z -= 0.5f;
    }

    if (win.isKeyPressed(GLFW_KEY_DOWN)) {
        g_light_pos.z += 0.5f;
    }

    if (win.isKeyPressed(GLFW_KEY_MINUS)) {
        g_iso_value -= 0.002f;
        g_iso_value = std::max(g_iso_value, 0.0f);
    }
    
    if (win.isKeyPressed(GLFW_KEY_EQUAL) || win.isKeyPressed(GLFW_KEY_KP_ADD)) {
        g_iso_value += 0.002f;
        g_iso_value = std::min(g_iso_value, 1.0f);
    }

    if (win.isKeyPressed(GLFW_KEY_D)) {
        g_sampling_distance -= 0.0001f;
        g_sampling_distance = std::max(g_sampling_distance, 0.0001f);
    }

    if (win.isKeyPressed(GLFW_KEY_S)) {
        g_sampling_distance += 0.0001f;
        g_sampling_distance = std::min(g_sampling_distance, 0.2f);
    }

    // to add key inputs:
    // check win.isKeyPressed(KEY_NAME)
    // - KEY_NAME - key name      (GLFW_KEY_A ... GLFW_KEY_Z)
    
    //if (win.isKeyPressed(GLFW_KEY_X)){
    //    
    //        ... do something
    //    
    //}

    /// reload shader if key R ist pressed
    if (win.isKeyPressed(GLFW_KEY_R)) {
        if (g_reload_shader_pressed != true) {
            GLuint newProgram(0);
            try {
                std::cout << "Reload shaders" << std::endl;
                newProgram = loadShaders(g_file_vertex_shader, g_file_fragment_shader);
            }
            catch (std::logic_error& e) {
                std::cerr << e.what() << std::endl;
                newProgram = 0;
            }
            if (0 != newProgram) {
                glDeleteProgram(program);
                program = newProgram;
            }
            g_reload_shader_pressed = true;
        }
    } else {
        g_reload_shader_pressed = false;
    }

    /// show transfer function if T is pressed
    if (win.isKeyPressed(GLFW_KEY_T)){
        if (!g_show_transfer_function_pressed){
            g_show_transfer_function = !g_show_transfer_function;
        }
        g_show_transfer_function_pressed = true;
    } else {
        g_show_transfer_function_pressed = false;
    }

    auto size = win.windowSize();
    glViewport(0, 0, size.x, size.y);
    glClearColor(g_background_color.x, g_background_color.y, g_background_color.z, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float fovy = 45.0f;
    float aspect = (float)size.x / (float)size.y;
    float zNear = 0.025f, zFar = 10.0f;
    glm::mat4 projection = glm::perspective(fovy, aspect, zNear, zFar);

    glm::vec3 translate = max_volume_bounds * glm::vec3(-0.5f);

    glm::vec3 eye = glm::vec3(0.0f, 0.0f, 1.5f);
    glm::vec3 target = glm::vec3(0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    auto view = glm::lookAt(eye, target, up);

    auto model_view = view
                    * manipulator.matrix(win)
                    // rotate head upright
                    * glm::rotate(0.5f*float(M_PI), glm::vec3(0.0f,1.0f,0.0f))
                    * glm::rotate(0.5f*float(M_PI), glm::vec3(1.0f,0.0f,0.0f))
                    * glm::translate(translate)
                    ;

    glm::vec4 camera_translate = glm::column(glm::inverse(model_view), 3);
    glm::vec3 camera_location = glm::vec3(camera_translate.x, camera_translate.y, camera_translate.z);

    camera_location /= glm::vec3(camera_translate.w);

    glm::vec4 light_location = glm::vec4(g_light_pos, 1.0f) * model_view;

    glUseProgram(program);

    glUniform1i(glGetUniformLocation(program, "volume_texture"), 0);
    glUniform1i(glGetUniformLocation(program, "transfer_texture"), 1);

    glUniform3fv(glGetUniformLocation(program, "camera_location"), 1,
        glm::value_ptr(camera_location));
    glUniform1f(glGetUniformLocation(program, "sampling_distance"), g_sampling_distance);
    glUniform1f(glGetUniformLocation(program, "iso_value"), g_iso_value);
    glUniform3fv(glGetUniformLocation(program, "max_bounds"), 1,
        glm::value_ptr(max_volume_bounds));
    glUniform3iv(glGetUniformLocation(program, "volume_dimensions"), 1,
        glm::value_ptr(vol_dimensions));

    glUniform3fv(glGetUniformLocation(program, "light_position"), 1,
        //glm::value_ptr(glm::vec3(light_location.x, light_location.y, light_location.z)));
        glm::value_ptr(g_light_pos));
    glUniform3fv(glGetUniformLocation(program, "light_color"), 1,
        glm::value_ptr(g_light_color));

    glUniform3fv(glGetUniformLocation(program, "light_color"), 1,
        glm::value_ptr(g_light_color));

    glUniformMatrix4fv(glGetUniformLocation(program, "Projection"), 1, GL_FALSE,
        glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(program, "Modelview"), 1, GL_FALSE,
        glm::value_ptr(model_view));
    cube.draw();
    glUseProgram(0);

    if (g_show_transfer_function)
        transfer_fun.update_and_draw();

    win.update();
  }

  return 0;
}
