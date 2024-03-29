/*
    CoffeeChain - open source engine for making games.
    Copyright (C) 2020-2022 Andrey Givoronsky

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA
*/

#include <stdio.h>
#include <stdlib.h>

#include "../external/glad.h"
#include <GLFW/glfw3.h>

#include "../../include/coffeechain/engine_common.h"
#include "../../include/coffeechain/utils.h"

#include "../engine_common_internal.h"

#define CCE_FULLSCREEN 0x10
#define CCE_WAYLAND 0x1

extern void (*cce__engineUpdate__api) (void);
extern void (*cce__terminateEngine__api) (void);

static struct
{
   GLFWwindow *window;
   const GLFWvidmode *vidMode;
   unsigned int windowWidth;
   unsigned int windowHeight;
   struct cce_u32vec2 monitorAspectRatio;
   int windowPositionX;
   int windowPositionY;
   uint8_t flags;
} g_GLFWstate;

static struct RegisteredKeys
{
   int key;
   int modifiers;
   uint16_t eventType; // 0x0 - boolChange, 0x1 - plotNumber, 0x2 - systemAction
   uint16_t number;
} *cce_keys;
static size_t keysQuantity = 0u;
//static size_t keysQuantityAllocated = CCE_ALLOCATION_STEP;

static double deltaTime;
static double lastTime = 0.0;
CCE_PUBLIC_OPTIONS const double *const cceDeltaTime = &deltaTime;
CCE_PUBLIC_OPTIONS const double *const cceCurrentTime = &lastTime;
static uint8_t engineFlags = 0u;
const uint8_t *const cce__flags = &engineFlags;
static uint8_t internalFlags = 0u;

static void calculateInternalDeltaTime (void)
{
   double currentTime = glfwGetTime();
   deltaTime = currentTime - lastTime;
   lastTime = currentTime;
}

static void engineUpdate__glfw (void)
{
   glfwPollEvents();
   if (glfwWindowShouldClose(g_GLFWstate.window))
   {
      engineFlags |= CCE_ENGINE_STOP;
   }
   calculateInternalDeltaTime();
}

static void framebufferSizeCallback (GLFWwindow *window, int width, int height)
{
   CCE_UNUSED(window);
   glViewport(0, 0, width, height);
}

static void windowResizeCallback (GLFWwindow *window, int width, int height)
{
   CCE_UNUSED(window);
   g_GLFWstate.windowWidth = width;
   g_GLFWstate.windowHeight = height;
}

void swapBuffers__glfw (void)
{
   glfwSwapBuffers(g_GLFWstate.window);
}

static size_t registerKey__glfw (int glfwKey, int glfwKeyModifiers, uint16_t eventType, uint16_t number)
{
   //if (keysQuantity >= keysQuantityAllocated)
   //{
   //   keysQuantityAllocated += CCE_ALLOCATION_STEP;
   //   cce_keys = realloc(cce_keys, keysQuantityAllocated);
   //}
   (cce_keys + keysQuantity)->key = glfwKey;
   (cce_keys + keysQuantity)->modifiers = glfwKeyModifiers;
   (cce_keys + keysQuantity)->eventType = eventType;
   (cce_keys + keysQuantity)->number = number;
   return keysQuantity++;
}

static void toFullscreen__glfw (void)
{
   GLFWmonitor *monitor = glfwGetPrimaryMonitor();
   g_GLFWstate.vidMode = glfwGetVideoMode(monitor);
   uint32_t aspectRatio = 0u, monitorAspectRatio = 0u;
   unsigned int width, height;
   glfwSetWindowSizeCallback(g_GLFWstate.window, NULL);
   if (g_GLFWstate.flags & CCE_FIXED_ASPECT_RATIO)
   {
      glfwSetFramebufferSizeCallback(g_GLFWstate.window, framebufferSizeCallback);
   }
   if (g_GLFWstate.flags & CCE_FIXED_RESOLUTION)
   {
      width = g_GLFWstate.windowWidth;
      height = g_GLFWstate.windowHeight;
   }
   else
   {
      width = g_GLFWstate.vidMode->width;
      height = g_GLFWstate.vidMode->height;
   }
   glfwGetWindowPos(g_GLFWstate.window, &g_GLFWstate.windowPositionX, &g_GLFWstate.windowPositionY);
   glfwSetWindowMonitor(g_GLFWstate.window, monitor, 0, 0, width, height, g_GLFWstate.vidMode->refreshRate);
   if (g_GLFWstate.flags & CCE_FIXED_ASPECT_RATIO)
   {
      aspectRatio = (g_GLFWstate.vidMode->width * 100u) / g_GLFWstate.vidMode->height;
      monitorAspectRatio = (g_GLFWstate.monitorAspectRatio.x * 100u) / g_GLFWstate.monitorAspectRatio.y;
      if (aspectRatio != monitorAspectRatio)
      {
         glfwSetFramebufferSizeCallback(g_GLFWstate.window, NULL);
         if (aspectRatio < monitorAspectRatio)
         {
            height = g_GLFWstate.windowHeight;
            width = (g_GLFWstate.windowHeight * monitorAspectRatio) / 100u;
         }
         else
         {
            width = g_GLFWstate.windowWidth;
            height = (g_GLFWstate.windowWidth * 100u) / monitorAspectRatio;
         }
      }
      glViewport((width - g_GLFWstate.windowWidth) / 2u, (height - g_GLFWstate.windowHeight) / 2u, g_GLFWstate.windowWidth, g_GLFWstate.windowHeight);
      glfwSetFramebufferSizeCallback(g_GLFWstate.window, framebufferSizeCallback);
   }
   glfwSetWindowSizeCallback(g_GLFWstate.window, windowResizeCallback);
   g_GLFWstate.flags |= CCE_FULLSCREEN;
   glfwSwapInterval(1);
}

static void toWindow__glfw (void)
{
   glfwSetWindowMonitor(g_GLFWstate.window, NULL, g_GLFWstate.windowPositionX, g_GLFWstate.windowPositionY, g_GLFWstate.windowWidth, g_GLFWstate.windowHeight, g_GLFWstate.vidMode->refreshRate);
   g_GLFWstate.flags &= ~CCE_FULLSCREEN;
   glfwSwapInterval(1);
}

static void showWindow__glfw (void)
{
   glfwShowWindow(g_GLFWstate.window);
}

static void keyCallback (GLFWwindow *window, int key, int scancode, int action, int modifiers)
{
   CCE_UNUSED(window);
   CCE_UNUSED(scancode);
   struct RegisteredKeys *keyInfo = cce_keys + cceBinarySearch((((uint8_t*) cce_keys) + offsetof(struct RegisteredKeys, key)), keysQuantity, sizeof(int), sizeof(struct RegisteredKeys), key);
   if (keyInfo > cce_keys + keysQuantity)
   {
      return;
   }
   if (keyInfo->key == key)
   {
      if ((modifiers & keyInfo->modifiers) == keyInfo->modifiers)
      {
         switch (keyInfo->eventType)
         {
            
            case 0x0u:
            {
               if (action != GLFW_REPEAT)
                  cceSetBool(keyInfo->number, action + CCE_DISABLE_BOOL);
               break;
            }
            case 0x1u:
            {
               if (action == GLFW_PRESS)
               {
                  cceIncreasePlotNumber(keyInfo->number);
                  break;
               }
               break;
            }
            case 0x2:
            {
               if (action == GLFW_PRESS)
               {
                  switch (keyInfo->number)
                  {
                     case 0x0:
                     {
                        if (g_GLFWstate.flags & CCE_FULLSCREEN)
                        {
                           toWindow__glfw();
                        }
                        else
                        {
                           toFullscreen__glfw();
                        }
                     }
                  }
               }
               break;
            }
         }
      }
   }
}

static struct cce_u32vec2 getAspectRatio (uint32_t width, uint32_t height)
{
   if (!width || !height)
   {
      return (struct cce_u32vec2) {0u, 0u};
   }
   uint32_t ratio = (width * 100u) / height;
   if (ratio < 800u/5u)
   {
      if (ratio < 400u/3u)
      {
         if (ratio < 500u/4u)
            return (struct cce_u32vec2) {1u, 1u};
         else
            return (struct cce_u32vec2) {5u, 4u};
      }
      else
      {
         if (ratio < 300u/2u)
         {
            return (struct cce_u32vec2) {4u, 3u};
         }
         else
         {
            if (ratio < 1400u/9u)
               return (struct cce_u32vec2) {3u, 2u};
            else
               return (struct cce_u32vec2) {14u, 9u};
         }
      }
   }
   else
   {
      if (ratio < 1600u/9u)
      {
         if (ratio < 500u/3u)
            return (struct cce_u32vec2) {8u, 5u};
         else
            return (struct cce_u32vec2) {5u, 3u};
      }
      else
      {
         if (ratio < 200u/1u)
         {
            return (struct cce_u32vec2) {16u, 9u};
         }
         else
         {
            if (ratio < 700u/3u)
               return (struct cce_u32vec2) {2u, 1u};
            else
               return (struct cce_u32vec2) {7u, 3u};
         }
      }
   }
}

struct cce_u32vec2 getCurrentStep__glfw (void)
{
   switch (g_GLFWstate.monitorAspectRatio.x)
   {
      case 1u:
      {
         return (struct cce_u32vec2) {12u, 12u};
      }
      case 3u:
      {
         return (struct cce_u32vec2) {15u, 10u};
      }
      case 5u:
      {
         return (struct cce_u32vec2) {15u, 9u};
      }
      case 7u:
      {
         return (struct cce_u32vec2) {19u, 8u};
      }
      case 14u:
      {
         return (struct cce_u32vec2) {14u, 9u};
      }
      default:
      {
         return (struct cce_u32vec2) {16u, (uint32_t) (16.0f / g_GLFWstate.monitorAspectRatio.x * g_GLFWstate.monitorAspectRatio.y)};
      }
   }
}

static void setWindowParameters__glfw (cce_enum parameter, uint32_t a, uint32_t b)
{
   if (a > 0u && b > 0u)
   {
      switch (parameter)
      {
         case CCE_FIXED_RESOLUTION:
         {
            g_GLFWstate.flags       |= CCE_FIXED_RESOLUTION | CCE_FIXED_ASPECT_RATIO;
            glfwSetWindowAspectRatio(g_GLFWstate.window, a, b);
            g_GLFWstate.windowWidth  = a;
            g_GLFWstate.windowHeight = b;
            glfwSetWindowSize(g_GLFWstate.window, a, b);
            g_GLFWstate.monitorAspectRatio = getAspectRatio(a, b);
            return;
         }
         case CCE_FIXED_ASPECT_RATIO:
         {
            g_GLFWstate.flags               &= ~CCE_FIXED_RESOLUTION;
            g_GLFWstate.flags               |=  CCE_FIXED_ASPECT_RATIO;
            glfwSetWindowAspectRatio(g_GLFWstate.window, a, b);
            g_GLFWstate.monitorAspectRatio.x = a;
            g_GLFWstate.monitorAspectRatio.y = b;
            return;
         }
         case CCE_MINIMAL_ASPECT_RATIO:
         {
            if ((g_GLFWstate.monitorAspectRatio.x * 100u) / g_GLFWstate.monitorAspectRatio.y < (a*100u)/b)
            {
               glfwSetWindowAspectRatio(g_GLFWstate.window, a, b);
            }
            return;
         }
         case CCE_MAXIMUM_ASPECT_RATIO:
         {
            if ((g_GLFWstate.monitorAspectRatio.x * 100u) / g_GLFWstate.monitorAspectRatio.y > (a*100u)/b)
            {
               glfwSetWindowAspectRatio(g_GLFWstate.window, a, b);
            }
            return;
         }
      }
   }
   else
   {
      GLFWmonitor *monitor = glfwGetPrimaryMonitor();
      g_GLFWstate.vidMode = glfwGetVideoMode(monitor);
      g_GLFWstate.monitorAspectRatio = getAspectRatio(g_GLFWstate.vidMode->width, g_GLFWstate.vidMode->height);
      g_GLFWstate.flags        = 0x0u;
      glfwSetWindowAspectRatio(g_GLFWstate.window, g_GLFWstate.windowWidth, g_GLFWstate.windowHeight);
      return;
   }
}

static int compare (const void *a, const void *b)
{
   const int num_a = *((int*) (((uint8_t*) a) + offsetof(struct RegisteredKeys, key)));
   const int num_b = *((int*) (((uint8_t*) b) + offsetof(struct RegisteredKeys, key)));
   return (num_a > num_b) - (num_a < num_b);
}

static void terminateEngine__glfw (void);

int cce__initEngine__glfw (const char *label, uint16_t globalBoolsQuantity)
{
   //AL = initAL();
   if (glfwInit() != GLFW_TRUE)
   {
      return -1;
   }
   
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
   glfwWindowHint(GLFW_AUTO_ICONIFY,   GLFW_TRUE);
   glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
   glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
   char *wl = getenv("WAYLAND_DISPLAY");
   internalFlags |= CCE_WAYLAND * (wl != NULL && (*wl != '\0'));
   if ((~internalFlags & CCE_WAYLAND) == CCE_WAYLAND)
   {
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
   }

#ifdef __APPLE__
   glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
   glfwWindowHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
   glfwWindowHint(GLFW_COCOA_MENUBAR,         GLFW_FALSE);
#endif
   
   
   if (!label)
   {
      label = CCE_DEFAULT_WINDOW_LABEL;
   }
   
   GLFWmonitor *monitor = glfwGetPrimaryMonitor();
   g_GLFWstate.vidMode = glfwGetVideoMode(monitor);
   g_GLFWstate.monitorAspectRatio = getAspectRatio(g_GLFWstate.vidMode->width, g_GLFWstate.vidMode->height);
   switch (g_GLFWstate.monitorAspectRatio.x)
   {
      case 1u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_1BY1;
         break;
      }
      case 2u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_2BY1;
         break;
      }
      case 3u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH_3BY2;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_3BY2;
         break;
      }
      case 4u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_4BY3;
         break;
      }
      case 5u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_5BY4;
         break;
      }
      case 7u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH_7BY3;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_7BY3;
         break;
      }
      case 8u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_8BY5;
         break;
      }
      case 14u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH_14BY9;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_14BY9;
         break;
      }
      case 16u:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_HEIGHT_16BY9;
         break;
      }
      default:
      {
         g_GLFWstate.windowWidth = CCE_DEFAULT_WINDOW_WIDTH;
         g_GLFWstate.windowHeight = CCE_DEFAULT_WINDOW_WIDTH / g_GLFWstate.monitorAspectRatio.x * g_GLFWstate.monitorAspectRatio.y;
      }
   }
   g_GLFWstate.window = glfwCreateWindow(g_GLFWstate.windowWidth, g_GLFWstate.windowHeight, label, NULL, NULL);
   if (!(g_GLFWstate.window))
   {
      fprintf(stderr, "GLFW::WINDOW::FAILED_TO_CREATE\n");
      glfwTerminate();
      return -1;
   }
   glfwMakeContextCurrent(g_GLFWstate.window);
   glfwSetFramebufferSizeCallback(g_GLFWstate.window, framebufferSizeCallback);
   glfwSetWindowSizeCallback(g_GLFWstate.window, windowResizeCallback);
   glfwSetKeyCallback(g_GLFWstate.window, keyCallback);
   if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress))
   {
      fprintf(stderr, "GLAD::INITIALIZATION::FAILED:\nOpenGL could not be loaded by GLAD.\n");
      glfwTerminate();
      return -1;
   }
   if (!GLAD_GL_VERSION_3_3)
   {
      fprintf(stderr, "GLAD::INITIALIZATION::FAILED:\nOpenGL 3.3 could not be loaded by GLAD.\n");
      glfwTerminate();
      return -1;
   }
   cce_keys = malloc(14u * sizeof(struct RegisteredKeys));
   
   registerKey__glfw(GLFW_KEY_UP,          GLFW_FALSE, 0x0, globalBoolsQuantity - 12);
   registerKey__glfw(GLFW_KEY_DOWN,        GLFW_FALSE, 0x0, globalBoolsQuantity - 11);
   registerKey__glfw(GLFW_KEY_LEFT,        GLFW_FALSE, 0x0, globalBoolsQuantity - 10);
   registerKey__glfw(GLFW_KEY_RIGHT,       GLFW_FALSE, 0x0, globalBoolsQuantity -  9);
   registerKey__glfw(GLFW_KEY_A,           GLFW_FALSE, 0x0, globalBoolsQuantity -  8);
   registerKey__glfw(GLFW_KEY_S,           GLFW_FALSE, 0x0, globalBoolsQuantity -  7);
   registerKey__glfw(GLFW_KEY_D,           GLFW_FALSE, 0x0, globalBoolsQuantity -  6);
   registerKey__glfw(GLFW_KEY_W,           GLFW_FALSE, 0x0, globalBoolsQuantity -  5);
   registerKey__glfw(GLFW_KEY_LEFT_SHIFT,  GLFW_FALSE, 0x0, globalBoolsQuantity -  4);
   registerKey__glfw(GLFW_KEY_RIGHT_SHIFT, GLFW_FALSE, 0x0, globalBoolsQuantity -  4);
   registerKey__glfw(GLFW_KEY_ENTER,       GLFW_FALSE, 0x0, globalBoolsQuantity -  3);
   registerKey__glfw(GLFW_KEY_Q,           GLFW_FALSE, 0x0, globalBoolsQuantity -  2);
   registerKey__glfw(GLFW_KEY_E,           GLFW_FALSE, 0x0, globalBoolsQuantity -  1);
   
   registerKey__glfw(GLFW_KEY_F11, GLFW_FALSE, 0x2, 0x0);
   qsort(cce_keys, keysQuantity, sizeof(struct RegisteredKeys), compare);
   
   glfwSwapInterval(1);
   
   cceSetWindowParameters = setWindowParameters__glfw;
   cce__toWindow = toWindow__glfw;
   if (internalFlags && CCE_WAYLAND)
   {
      cce__showWindow = cce__doNothing;
   }
   else
   {
      cce__showWindow = showWindow__glfw;
      glfwSetWindowAspectRatio(g_GLFWstate.window, g_GLFWstate.windowWidth, g_GLFWstate.windowHeight);
   }
   cce__toFullscreen = toFullscreen__glfw;
   cce__swapBuffers = swapBuffers__glfw;
   cce__getCurrentStep = getCurrentStep__glfw;
   cce__engineUpdate__api = engineUpdate__glfw;
   cce__terminateEngine__api = terminateEngine__glfw;
   return 0;
}

static void terminateEngine__glfw (void)
{
   free(cce_keys);
   glfwDestroyWindow(g_GLFWstate.window);
   glfwTerminate();
}
