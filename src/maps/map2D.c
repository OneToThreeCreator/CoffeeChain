/*
    CoffeeChain - open source engine for making games.
    Copyright (C) 2020-2021 Andrey Givoronsky

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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>

#include "../engine_common.h"
#include "../engine_common_internal.h"
#include "../shader.h"
#include "../path_getters.h"
#include "../external/stb_image.h"
#include "map2D.h"
#include "map2D_internal.h"

#define BASIC_ACTIONS_QUANTITY 16u

static void (**cce_actions)(void*);

static uint32_t                   g_texturesWidth;
static uint32_t                   g_texturesHeight;
static struct LoadedTextures     *g_textures;
static uint16_t                   g_texturesQuantity;
static uint16_t                   g_texturesQuantityAllocated;
static GLuint                     glTexturesArray = 0u;
static uint16_t                   glTexturesArraySize = 0u;
static struct UsedUBO            *g_UBOs;
static uint16_t                   g_UBOsQuantity;
static uint16_t                   g_UBOsQuantityAllocated;
static GLint                      g_uniformBufferSize;
static const struct DynamicMap2D *g_dynamicMap;
static struct cce_ivec2           globalOffsetCoords = {0, 0};

static void (*cce_setUniformBufferToDefault)(GLuint, GLint);
static GLuint shaderProgram;
static GLint *bufferUniformsOffsets;
static GLint *uniformLocations;

static uint32_t actionsQuantity;

static char *texturesPath = NULL;
static size_t texturesPathLength;

static cce_flag map2Dflags;

/* CBO - clear buffer object. Requires function glClearBufferSubData to present in openGL */
static void setUniformBufferToDefault_withCBOext (GLuint UBO, GLint RotateAngleCosOffset)
{
   glBindBuffer(GL_UNIFORM_BUFFER, UBO);
   GL_CHECK_ERRORS;
   glClearBufferSubData(GL_UNIFORM_BUFFER, GL_R32F, 0u, g_uniformBufferSize, GL_RED, GL_FLOAT, NULL);
   GL_CHECK_ERRORS;
   float one = 1;
   glClearBufferSubData(GL_UNIFORM_BUFFER, GL_R32F, RotateAngleCosOffset, 255u * sizeof(float) , GL_RED, GL_FLOAT, &one);
   GL_CHECK_ERRORS;
}

static const float *g_ones;

static void setUniformBufferToDefault_withoutCBOext (GLuint UBO, GLint RotateAngleCosOffset)
{
   glBindBuffer(GL_UNIFORM_BUFFER, UBO);
   GL_CHECK_ERRORS;
   void *uboData = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
   GL_CHECK_ERRORS;
   memset(uboData, 0, g_uniformBufferSize);
   memcpy(uboData + RotateAngleCosOffset, g_ones, 255 * sizeof(float));
   glUnmapBuffer(GL_UNIFORM_BUFFER);
   GL_CHECK_ERRORS;
}

static inline void drawMap2D (struct Map2D *map)
{
   glBindVertexArray(map->VAO);
   GL_CHECK_ERRORS;
   glBindBufferRange(GL_UNIFORM_BUFFER, 1u, (g_UBOs + map->UBO_ID)->UBO, 0u, g_uniformBufferSize);
   GL_CHECK_ERRORS;
   glDrawArrays(GL_POINTS, 0u, map->elementsQuantity);
   GL_CHECK_ERRORS;
}

static void drawMap2Dmain (struct Map2Darray *maps)
{
   drawMap2D(maps->main);
}

static struct Map2D *lastNearestMap2D = NULL;

static void drawMap2Dnearest (struct Map2Darray *maps)
{
   drawMap2D(maps->main);
   drawMap2D(lastNearestMap2D);
}

static void drawMap2Dall (struct Map2Darray *maps)
{
   drawMap2D(maps->main);
   for (struct Map2D **iterator = maps->dependies, **end = maps->dependies + maps->main->exitMapsQuantity; iterator < end; ++iterator)
   {
      drawMap2D(*iterator);
   }
}

static void processLogicMap2Dmain (struct Map2Darray *maps)
{
   processLogicMap2D(maps->main);
}

static void processLogicMap2Dnearest (struct Map2Darray *maps)
{
   processLogicMap2D(maps->main);
   processLogicMap2D(lastNearestMap2D);
}

static void processLogicMap2Dall (struct Map2Darray *maps)
{
   processLogicMap2D(maps->main);
   for (struct Map2D **iterator = maps->dependies, **end = maps->dependies + maps->main->exitMapsQuantity; iterator < end; ++iterator)
   {
      processLogicMap2D((*iterator));
   }
}

static void (*drawMap2Dcommon) (struct Map2Darray*);
static void (*processLogicMap2Dcommon) (struct Map2Darray*);

CCE_PUBLIC_OPTIONS void cceSetFlags2D (cce_flag flags)
{
   switch (flags & (CCE_RENDER_ONLY_CURRENT_MAP | CCE_RENDER_CLOSEST_MAP | CCE_RENDER_ALL_LOADED_MAPS))
   {
      case CCE_RENDER_ONLY_CURRENT_MAP:
      {
         drawMap2Dcommon = drawMap2Dmain;
         break;
      }
      case CCE_RENDER_CLOSEST_MAP:
      {
         drawMap2Dcommon = drawMap2Dnearest;
         break;
      }
      case CCE_RENDER_ALL_LOADED_MAPS:
      {
         drawMap2Dcommon = drawMap2Dall;
         break;
      }
   }
   
   switch (flags & (CCE_PROCESS_LOGIC_ONLY_FOR_CURRENT_MAP | CCE_PROCESS_LOGIC_FOR_CLOSEST_MAP | CCE_PROCESS_LOGIC_FOR_ALL_MAPS))
   {
      case CCE_PROCESS_LOGIC_ONLY_FOR_CURRENT_MAP:
      {
         processLogicMap2Dcommon = processLogicMap2Dmain;
         break;
      }
      case CCE_PROCESS_LOGIC_FOR_CLOSEST_MAP:
      {
         processLogicMap2Dcommon = processLogicMap2Dnearest;
         break;
      }
      case CCE_PROCESS_LOGIC_FOR_ALL_MAPS:
      {
         processLogicMap2Dcommon = processLogicMap2Dall;
         break;
      }
   }
}

static GLuint createTextureArray (uint16_t newSize)
{
   GLuint texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, g_texturesWidth, g_texturesHeight, newSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   GL_CHECK_ERRORS;   
   
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   
   return texture;
}

CCE_PUBLIC_OPTIONS void cceSetGridMultiplier (float multiplier)
{
   struct cce_uvec2 aspectRatio = cce__getCurrentStep();
   glUniform2f(*uniformLocations, aspectRatio.x * multiplier, aspectRatio.y * multiplier);
}

CCE_PUBLIC_OPTIONS int cceInitEngine2D (uint16_t globalBoolsQuantity, uint32_t textureMaxWidth, uint32_t textureMaxHeight,
                                        const char *windowLabel, const char *resourcePath)
{
   if (!resourcePath)
   {
      resourcePath = getenv("CCE_RESOURCE_PATH");
      if (!resourcePath || *resourcePath == '\0')
      {
         fputs("ENGINE::INIT::NO_RESOURCE_PATH:\nEngine could not load the game without knowing where it is", stderr);
         return -1;
      }
   }
   size_t pathLength = strlen(resourcePath) + 1u;
   char *pathBuffer = malloc((pathLength + 11u) * sizeof(char));
   memcpy(pathBuffer, resourcePath, pathLength);
   *(pathBuffer + pathLength) = '\0';
   
   map2Dflags = CCE_INIT;
   if (cce__initEngine(windowLabel, globalBoolsQuantity) != 0)
   {
      free(pathBuffer);
      return -1;
   }
   
   {
      char string[] = "#define GLOBAL_OFFSET_CONTROL_MASK " MACRO_TO_STR(CCE_GLOBAL_OFFSET_MASK) "\n";
      #ifdef SYSTEM_SHADER_PATH
      shaderProgram = makeVGFshaderProgram(SYSTEM_SHADER_PATH "/vertex_shader.glsl", SYSTEM_SHADER_PATH "/geometry_shader.glsl", SYSTEM_SHADER_PATH "/fragment_shader.glsl", 330u, string, "", "");
      if (shaderProgram == 0u)
      #endif // SYSTEM_SHADER_PATH
      {
         cceAppendPath(pathBuffer, pathLength + 11u, "shaders");
         char *vertexPath   = cce__createNewPathFromOldPath(pathBuffer, "vertex_shader.glsl",   0u);
         char *geometryPath = cce__createNewPathFromOldPath(pathBuffer, "geometry_shader.glsl", 0u);
         char *fragmentPath = cce__createNewPathFromOldPath(pathBuffer, "fragment_shader.glsl", 0u);
         shaderProgram = makeVGFshaderProgram(vertexPath, geometryPath, fragmentPath, 330u, string, "", "");
         free(vertexPath);
         free(geometryPath);
         free(fragmentPath);
         *(pathBuffer + pathLength) = '\0';
      }
   }
   if (!shaderProgram)
   {
      fputs("ENGINE::INIT::SHADERS_CANNOT_BE_LOADED", stderr);
      cce__terminateEngine();
      free(pathBuffer);
      return -1;
   }
   
   uniformLocations = malloc(2 * sizeof(GLint));
   *uniformLocations = glGetUniformLocation(shaderProgram, "Step");
   GL_CHECK_ERRORS;
   *(uniformLocations + 1) = glGetUniformLocation(shaderProgram, "GlobalMoveCoords");
   GL_CHECK_ERRORS;
   {
      const GLchar *uniformNames[] = {"Colors", "MoveCoords", "Extention", "TextureOffset", "RotationOffset", "RotateAngleSin", "RotateAngleCos"};
      GLuint indices[7];
      glGetUniformIndices(shaderProgram, 7, uniformNames, indices);
      GL_CHECK_ERRORS;
      bufferUniformsOffsets = (GLint*) malloc(7 * sizeof(GLint));
      glGetActiveUniformsiv(shaderProgram, 7, indices, GL_UNIFORM_OFFSET, bufferUniformsOffsets);
      GL_CHECK_ERRORS;
      glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "Variables"), 1u);
      GL_CHECK_ERRORS;
   }
   g_UBOsQuantityAllocated = CCE_ALLOCATION_STEP;
   g_UBOs = (struct UsedUBO*) malloc(g_UBOsQuantityAllocated * sizeof(struct UsedUBO));
   {
      GLint maxUniformOffset = 0;
      uint8_t i = 0, maxI;
      for (GLint *iterator = bufferUniformsOffsets, *end = bufferUniformsOffsets + 7u; iterator < end; ++iterator, ++i)
      {
         if (maxUniformOffset < (*iterator))
         {
            maxUniformOffset = (*iterator);
            maxI = i;
         }
      }
      switch (maxI)
      {
         case 0:
         {
            g_uniformBufferSize = maxUniformOffset + (4/*GLint and GLfloat*/ * 4/*vec4*/ * 255/*array*/);
            break;
         }
         case 1:
         case 2:
         case 3:
         case 4:
         {
            g_uniformBufferSize = maxUniformOffset + (4/*GLint and GLfloat*/ * 2/*vec2*/ * 255/*array*/);
            break;
         }
         case 5:
         case 6:
         {
            g_uniformBufferSize = maxUniformOffset + (4/*GLint and GLfloat*/ * 255/*array*/);
            break;
         }
      }
   }
   if (GLAD_GL_ARB_clear_buffer_object)
   {
      cce_setUniformBufferToDefault = setUniformBufferToDefault_withCBOext;
   }
   else
   {
      cce_setUniformBufferToDefault = setUniformBufferToDefault_withoutCBOext;
      float *ones = malloc(255 * sizeof(float));
      for (float *iterator = ones, *end = ones + 255; iterator < end; ++iterator)
      {
         *iterator = 1.0f;
      }
      g_ones = ones;
   }
   
   for (struct UsedUBO *iterator = g_UBOs, *end = g_UBOs + g_UBOsQuantityAllocated; iterator < end; ++iterator)
   {
      glGenBuffers(1u, &(iterator->UBO));
      GL_CHECK_ERRORS;
      glBindBuffer(GL_UNIFORM_BUFFER, iterator->UBO);
      GL_CHECK_ERRORS;
      glBufferData(GL_UNIFORM_BUFFER, (g_uniformBufferSize), NULL, GL_DYNAMIC_DRAW);
      GL_CHECK_ERRORS;
      iterator->flags = 0u;
      cce_setUniformBufferToDefault(iterator->UBO, *(bufferUniformsOffsets + 6));
   }
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   cce__initMap2DLoaders(&cce_actions);
   cceAppendPath(pathBuffer, pathLength + 11, "maps");
   cceSetMap2Dpath(pathBuffer);
   *(pathBuffer + pathLength) = '\0';
   
   actionsQuantity = CCE_BASIC_ACTIONS_QUANTITY + CCE_ALLOCATION_STEP;
   cce_actions = (void (**)(void*)) calloc((actionsQuantity), sizeof(void (*)(void*)));
   g_texturesWidth = textureMaxWidth;
   g_texturesHeight = textureMaxHeight;
   g_textures = (struct LoadedTextures*) calloc(CCE_ALLOCATION_STEP, sizeof(struct LoadedTextures));
   g_texturesQuantity = 0u;
   g_texturesQuantityAllocated = CCE_ALLOCATION_STEP;
   glTexturesArray = createTextureArray(CCE_ALLOCATION_STEP);
   glTexturesArraySize = CCE_ALLOCATION_STEP;
   stbi_set_flip_vertically_on_load(1);
   cceAppendPath(pathBuffer, pathLength + 11, "textures");
   cceSetTexturesPath(resourcePath);
   *(pathBuffer + pathLength) = '\0';
   g_dynamicMap = cce__initDynamicMap2D();
   cce__baseActionsInit(g_dynamicMap, g_UBOs, bufferUniformsOffsets, uniformLocations, shaderProgram, cce_setUniformBufferToDefault);
   cceSetFlags2D(CCE_DEFAULT);
   map2Dflags &= ~CCE_INIT;
   free(pathBuffer);
   glUseProgram(shaderProgram);
   cceSetGridMultiplier(1.0f);
   return 0;
}

CCE_PUBLIC_OPTIONS uint8_t cceRegisterAction (uint32_t ID, void (*action)(void*))
{
   if (ID >= actionsQuantity)
   {
      uint32_t lastActionsQuantity = actionsQuantity;
      actionsQuantity = (ID & (CCE_ALLOCATION_STEP - 1u)) + CCE_ALLOCATION_STEP;
      cce_actions = (void (**)(void*)) realloc(cce_actions, actionsQuantity * sizeof(void (*)(void*)));
      memset(cce_actions + lastActionsQuantity, 0u, actionsQuantity - lastActionsQuantity);
   }
   if ((ID < BASIC_ACTIONS_QUANTITY) != ((map2Dflags & CCE_BASIC_ACTIONS_NOT_SET) > 0u))
      return CCE_ATTEMPT_TO_OVERRIDE_DEFAULT_ELEMENT;
   *(cce_actions + ID) = action;
   return 0u;
}

CCE_PUBLIC_OPTIONS void cceSetTexturesPath (const char *path)
{
   free(texturesPath);
   texturesPath = cce__createNewPathFromOldPath(path, "img_", 10u);
   texturesPathLength = strlen(texturesPath);
}

void cce__updateTexturesArray (void)
{
   int width, height, nrChannels;
   uint8_t arrayResized = 0u;
   cce_ubyte *data;
   GLuint textureArray;
   if (glTexturesArraySize < g_texturesQuantityAllocated)
   {
      textureArray = createTextureArray(g_texturesQuantityAllocated);
      glBindBuffer(GL_READ_BUFFER, glTexturesArray);
      GL_CHECK_ERRORS;
      arrayResized = 1u;
      glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
   }
   else
   {
      textureArray = glTexturesArray;
   }
   for (struct LoadedTextures *iterator = g_textures, *end = g_textures + g_texturesQuantity; iterator < end; ++iterator)
   {
      if (iterator->dependantMapsQuantity > 0u)
      {
         if ((iterator->flags & CCE_LOADEDTEXTURES_TOBELOADED))
         {
            shortToString(texturesPath, iterator->ID, ".png");
            data = stbi_load(texturesPath, &width, &height, &nrChannels, 4);
            *(texturesPath + texturesPathLength) = '\0';
            if (!data)
            {
               memcpy((texturesPath + texturesPathLength), "dummy.png", 10u);
               data = stbi_load(texturesPath, &width, &height, &nrChannels, 4);
               *(texturesPath + texturesPathLength) = '\0';
               if (!data)
               {
                  shortToString(texturesPath, iterator->ID, ".png");
                  cce__criticalErrorPrint("ENGINE::TEXTURE::DUMMY::FAILED_TO_LOAD:\nFailed to load dummy texture requested because %s was not found.", texturesPath);
               }
            }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, g_texturesWidth - width, g_texturesHeight - height, (iterator - g_textures), width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
            GL_CHECK_ERRORS;
            stbi_image_free(data);
            iterator->flags &= ~CCE_LOADEDTEXTURES_TOBELOADED;
         }
         else if (arrayResized)
         {
            glCopyTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 0, 0, g_texturesWidth, g_texturesHeight);
            GL_CHECK_ERRORS;
         }
      }
      else 
      {
         iterator->ID = 0u;
      }
   }
   if (arrayResized)
   {
      glDeleteTextures(1, &glTexturesArray);
      GL_CHECK_ERRORS;
      glTexturesArray = textureArray;
   }
   map2Dflags &= ~CCE_PROCESS_TEXTURES;
   return;
}

static void updateUBOarray (void)
{
   for (struct UsedUBO *iterator = g_UBOs, *end = g_UBOs + g_UBOsQuantity; iterator < end; ++iterator)
   {
      if (iterator->flags & 0x2)
      {
         iterator->flags &= 0x1;
         cce_setUniformBufferToDefault(iterator->UBO, *(bufferUniformsOffsets + 6));
      }
   }
}

uint16_t cce__getFreeUBO (void)
{
   map2Dflags |= CCE_PROCESS_UBO_ARRAY;
   for (struct UsedUBO *iterator = g_UBOs, *end = g_UBOs + g_UBOsQuantity; iterator < end; ++iterator)
   {
      if (iterator->flags & 0x1)
         continue;
      iterator->flags |= 0x1;
      return (uint16_t) (iterator - g_UBOs);
   }
   if (g_UBOsQuantity >= g_UBOsQuantityAllocated)
   {
      g_UBOsQuantityAllocated += CCE_ALLOCATION_STEP;
      g_UBOs = realloc(g_UBOs, g_UBOsQuantityAllocated * sizeof(struct UsedUBO));
      for (struct UsedUBO *iterator = g_UBOs + g_UBOsQuantityAllocated - CCE_ALLOCATION_STEP, *end = g_UBOs + g_UBOsQuantityAllocated; iterator < end; ++iterator)
      {
         glGenBuffers(1u, &(iterator->UBO));
         GL_CHECK_ERRORS;
         glBindBuffer(GL_UNIFORM_BUFFER, iterator->UBO);
         GL_CHECK_ERRORS;
         glBufferData(GL_UNIFORM_BUFFER, (g_uniformBufferSize), NULL, GL_DYNAMIC_DRAW);
         GL_CHECK_ERRORS;
         iterator->flags = 0u;
         cce_setUniformBufferToDefault(iterator->UBO, *(bufferUniformsOffsets + 6));
      }
   }
   struct UsedUBO *ubo = g_UBOs + g_UBOsQuantity;
   ubo->flags |= 0x1;
   return g_UBOsQuantity++;
}

void cce__releaseUBO (uint16_t ID)
{
   (g_UBOs + ID)->flags = 0x2;
   return;
}

void cce__releaseUnusedUBO (uint16_t ID)
{
   (g_UBOs + ID)->flags = 0x0;
   return;
}

static void extendLoadedTextures (uint16_t amount)
{
   g_texturesQuantityAllocated = (amount & ~(CCE_ALLOCATION_STEP - 1u)) + CCE_ALLOCATION_STEP;
   g_textures = realloc(g_textures, g_texturesQuantityAllocated * sizeof(struct LoadedTextures));
}

uint16_t cce__loadTexture (uint32_t textureID)
{
   if (textureID == 0u)
      return 0u;
   map2Dflags |= CCE_PROCESS_TEXTURES;
   uint16_t current_g_texture = 0u;
   for (;;)
   {
      if (current_g_texture < g_texturesQuantity)
      {
         current_g_texture = g_texturesQuantity;
         ++g_texturesQuantity;
         if (g_texturesQuantity > g_texturesQuantityAllocated)
         {
            extendLoadedTextures(CCE_ALLOCATION_STEP);
         }
         break;
      }
      if ((g_textures + current_g_texture)->ID == (textureID - 1u))
      {
         ++((g_textures + current_g_texture)->dependantMapsQuantity);
         return current_g_texture + 1u;
      }
      if ((g_textures + current_g_texture)->dependantMapsQuantity == 0u)
      {
         uint16_t i = current_g_texture;
         while (i < g_texturesQuantity)
         {
            if ((g_textures + i)->ID == (textureID - 1u))
            {
               ++((g_textures + i)->dependantMapsQuantity);
               return i + 1u;
            }
            ++i;
         }
         break;
      }
      ++current_g_texture;
   }
   (g_textures + current_g_texture)->ID = (textureID - 1u);
   (g_textures + current_g_texture)->flags = CCE_LOADEDTEXTURES_TOBELOADED;
   (g_textures + current_g_texture)->dependantMapsQuantity = 1u;
   return current_g_texture + 1u;
}

uint16_t* cce__loadTexturesMap2D (struct Map2DElement *elements, uint32_t elementsQuantity, uint16_t *texturesLoadedMapReliesOnQuantity)
{
   map2Dflags |= CCE_PROCESS_TEXTURES;
   uint32_t *texturesMapReliesOn = NULL;
   uint16_t  texturesMapReliesOnQuantity = 0u, texturesMapReliesOnAllocated = 0u;
   cce_ubyte isLoaded = 0u;
   for (struct Map2DElement *iterator = elements, *end = elements + elementsQuantity - 1u; iterator < end; ++iterator)
   {
      uint32_t ID = iterator->textureInfo.ID;
      if (ID == 0u) continue;
      for (uint32_t *jiterator = texturesMapReliesOn, *jend = texturesMapReliesOn + texturesMapReliesOnQuantity; jiterator < jend; ++jiterator)
      {
         if ((*jiterator) == (ID - 1u))
         {
            isLoaded = 1u;
            break;
         }
      }
      if (!isLoaded)
      {
         if (texturesMapReliesOnQuantity == texturesMapReliesOnAllocated)
         {
            texturesMapReliesOnAllocated += CCE_ALLOCATION_STEP;
            texturesMapReliesOn = (uint32_t*) realloc(texturesMapReliesOn, texturesMapReliesOnAllocated * sizeof(uint32_t));
         }
         (*(texturesMapReliesOn + texturesMapReliesOnQuantity)) = (ID - 1u); // 0u is invalid for openGL shaders, but perfectly fine here
         ++texturesMapReliesOnQuantity;
      }
   }
   uint16_t *texturesLoadedMapReliesOn = (uint16_t*) texturesMapReliesOn;
   uint16_t *literator = texturesLoadedMapReliesOn;
   uint16_t *end = texturesLoadedMapReliesOn + texturesMapReliesOnQuantity;
   uint32_t *jiterator, *kiterator = texturesMapReliesOn;
   uint32_t tmp;
   uint16_t *freeLoadedTextures = NULL;
   uint16_t freeLoadedTexturesQuantity = 0u, freeLoadedTexturesAllocated = 0u;
   isLoaded = 0u;
   for (uint16_t current_g_texture = 0u; current_g_texture < g_texturesQuantity; ++current_g_texture)
   {
      jiterator = kiterator;
      if ((g_textures + current_g_texture)->ID)
      {
         for (uint16_t *iterator = literator; iterator < end; ++iterator, ++jiterator)
         {
            if ((*jiterator) == ((g_textures + current_g_texture)->ID))
            {
               tmp = (*jiterator);
               (*jiterator) = (*kiterator);
               (*kiterator) = tmp;
               (*literator) = current_g_texture + 1u;
               elements->textureInfo.ID = current_g_texture + 1u;
               ++((g_textures + current_g_texture)->dependantMapsQuantity);
               ++literator;
               ++kiterator;
               ++elements;
               isLoaded = 1u;
               break;
            }
         }
         if ((!isLoaded) && (!(g_textures + current_g_texture)->dependantMapsQuantity) && ((end - literator) > freeLoadedTexturesQuantity))
         {
            if (freeLoadedTexturesQuantity == freeLoadedTexturesAllocated)
            {
               freeLoadedTexturesAllocated += CCE_ALLOCATION_STEP;
               freeLoadedTextures = (uint16_t *) realloc(freeLoadedTextures, freeLoadedTexturesAllocated * sizeof(uint16_t));
            }
            (*(freeLoadedTextures + freeLoadedTexturesQuantity)) = current_g_texture;
            ++freeLoadedTexturesQuantity;
         }
      }
      else if ((end - literator) > freeLoadedTexturesQuantity)
      {
         if (freeLoadedTexturesQuantity == freeLoadedTexturesAllocated)
         {
            freeLoadedTexturesAllocated += CCE_ALLOCATION_STEP;
            freeLoadedTextures = (uint16_t *) realloc(freeLoadedTextures, freeLoadedTexturesAllocated * sizeof(uint16_t));
         }
         (*(freeLoadedTextures + freeLoadedTexturesQuantity)) = current_g_texture;
         ++freeLoadedTexturesQuantity;
      }
   }
   uint16_t *iterator = freeLoadedTextures, *iend = freeLoadedTextures + freeLoadedTexturesQuantity;
   while ((literator < end) && (iterator < iend))
   {
      ((g_textures + (*iterator))->ID) = *kiterator;
      (*literator) = (*iterator) + 1u;
      elements->textureInfo.ID = (*iterator) + 1u;
      ((g_textures + (*iterator))->dependantMapsQuantity) = 1;
      ((g_textures + (*iterator))->flags) = CCE_LOADEDTEXTURES_TOBELOADED;
      ++literator, ++kiterator, ++iterator, ++elements;
   }
   free(freeLoadedTextures);
   uint16_t current_g_texture = g_texturesQuantity;
   while (literator < end)
   {
      if (current_g_texture >= g_texturesQuantity)
      {
         ++g_texturesQuantity;
         if (g_texturesQuantity > g_texturesQuantityAllocated)
         {
            extendLoadedTextures(CCE_ALLOCATION_STEP);
         }
      }
      ((g_textures + current_g_texture)->ID) = (*kiterator);
      (*literator) = current_g_texture + 1u; // 0u is invalid for openGL shaders (it's the way to say "We don't need texture here"), but perfectly fine here
      elements->textureInfo.ID = current_g_texture + 1u;
      ((g_textures + current_g_texture)->dependantMapsQuantity) = 1;
      ((g_textures + current_g_texture)->flags) = CCE_LOADEDTEXTURES_TOBELOADED;
      ++literator, ++kiterator, ++current_g_texture, ++elements;
   }
   *texturesLoadedMapReliesOnQuantity = texturesMapReliesOnQuantity;
   return (uint16_t*) realloc(texturesMapReliesOn, texturesMapReliesOnQuantity * sizeof(uint16_t));
}

void cce__releaseTextures (uint16_t *texturesMapReliesOn, uint16_t texturesMapReliesOnQuantity)
{
   for (uint16_t *iterator = texturesMapReliesOn, *end = texturesMapReliesOn + texturesMapReliesOnQuantity; iterator < end; ++iterator)
   {
      --((g_textures + (*(iterator) - 1u))->dependantMapsQuantity);
   }
   free(texturesMapReliesOn);
   return;
}

void cce__releaseTexture (uint16_t textureID)
{
   if (textureID == 0u)
      return;
   
   --((g_textures + (textureID - 1u))->dependantMapsQuantity);
   return;
}

cce_ubyte cce__fourthLogicTypeFuncMap2D(uint16_t ID, va_list argp)
{
   struct Map2D *map = (struct Map2D*) va_arg(argp, struct Map2D*);
   uint32_t *group1IDs = ((map->collisionGroups + (map->collision + ID)->group1)->elementIDs);
   uint32_t *group1lastID = (group1IDs + (map->collisionGroups + (map->collision + ID)->group1)->elementsQuantity - 1u);
   uint32_t *group2firstID = ((map->collisionGroups + (map->collision + ID)->group2)->elementIDs);
   uint32_t *group2IDs;
   uint32_t *group2lastID = (group2firstID + (map->collisionGroups + (map->collision + ID)->group2)->elementsQuantity - 1u);
   struct Map2DCollider *element1, *element2;
   while (group1IDs <= group1lastID)
   {
      group2IDs = group2firstID;
      while (group2IDs <= group2lastID)
      {
         element1 = (map->colliders + *group1IDs);
         element2 = (map->colliders + *group2IDs);
         // ignore comparing with itself
         if ((element1 != element2) && cceCheckCollisionMap2D(element1, element2))
         {
            return 1u;
         }
         ++group2IDs;
      }
      ++group1IDs;
   }
   return 0u;
}

static void swapMap2D (struct Map2D **a, struct Map2D **b)
{
   struct Map2D *tmp = (*a);
   (*a) = (*b);
   (*b) = tmp;
}

/* Manages dynamic memory! */
static struct Map2Darray* loadMap2DwithDependies (struct Map2Darray *maps, uint16_t number)
{
   if (!maps)
   {
      maps = (struct Map2Darray*) calloc(1u, sizeof(struct Map2Darray));
   }
   if (maps->main)
   {
      uint8_t oldExitMapsQuantity = maps->main->exitMapsQuantity;
      if (maps->main->ID != number)
      {
         if (maps->dependies)
         {
            for (struct Map2D **iterator = maps->dependies, **end = (maps->dependies + oldExitMapsQuantity - 1u);; ++iterator)
            {
               if (((*iterator)->ID) == number)
               {
                  swapMap2D(&(maps->main), iterator);
                  break;
               }
               if (iterator >= end)
               {
                  cceFreeMap2D(maps->main);
                  maps->main = cceLoadMap2D(number);
                  break;
               }
            }
         }
         else
         {
            cceFreeMap2D(maps->main);
            maps->main = cceLoadMap2D(number);
         }
      }
      if (!(maps->main->exitMapsQuantity))
      {
         if (maps->dependies)
         {
            for (struct Map2D **iterator = maps->dependies, **end = (maps->dependies + oldExitMapsQuantity - 1u); iterator <= end; ++iterator)
            {
               cceFreeMap2D((*iterator));
            }
            free(maps->dependies);
            maps->dependies = NULL;
         }
         return maps;
      }
      struct Map2D **dependies = (struct Map2D**) malloc(maps->main->exitMapsQuantity * sizeof(struct Map2D*));
      struct Map2D **j = dependies;
      for (struct ExitMap2D *i = maps->main->exitMaps, *iend = (maps->main->exitMaps + maps->main->exitMapsQuantity - 1u); i <= iend; ++i, ++j)
      {
         for (struct Map2D **k = maps->dependies, **kend = (maps->dependies + oldExitMapsQuantity);; ++k)
         {
            if (!(*k)) continue;
            if ((*k)->ID == i->ID)
            {
               (*j) = (*k);
               (*k) = NULL;
               break;
            }
            if (k >= kend)
            {
               (*j) = cceLoadMap2D(i->ID);
               break;
            }
         }
      }
      for (struct Map2D **iterator = maps->dependies, **end = (maps->dependies + oldExitMapsQuantity); iterator < end; ++iterator)
      {
         cceFreeMap2D((*iterator));
      }
      free(maps->dependies);
      maps->dependies = dependies;
   }
   else
   {
      maps->main = cceLoadMap2D(number);
      if (maps->dependies)
      {
         cce__errorPrint("ENGINE::MAP2DARRAY_LOADER::DEPENDENCY_OF_NOTHING:\nMaps->dependies initialized without maps->main. Impossible to free maps->dependies. Possible memory leak", NULL);
      }
      maps->dependies = (struct Map2D**) malloc(maps->main->exitMapsQuantity * sizeof(struct Map2D*));
      struct ExitMap2D *exitmap = maps->main->exitMaps;
      for (struct Map2D **iterator = maps->dependies, **end = (maps->dependies + maps->main->exitMapsQuantity - 1u); iterator <= end; ++iterator, ++exitmap)
      {
         (*iterator) = cceLoadMap2D(exitmap->ID);
      }
   }
   return maps;
}

static inline int getMapBorderDistance (struct ExitMap2D *borderInfo)
{
   int32_t globalOffsetA, globalOffsetB;
   if (borderInfo->flags & 1u)
   {
      globalOffsetA = globalOffsetCoords.x;
      globalOffsetB = globalOffsetCoords.y;
   }
   else
   {
      globalOffsetA = globalOffsetCoords.y;
      globalOffsetB = globalOffsetCoords.x;
   }
   if (borderInfo->b1Border < globalOffsetB && borderInfo->b2Border < globalOffsetB)
   {
      if (borderInfo->flags & 0x2)
      {
         return -(borderInfo->aBorder - globalOffsetA);
      }
      else
      {
         return borderInfo->aBorder - globalOffsetA;
      }
   }
   else
   {
      return INT32_MAX;
   }
}

void cce__terminateEngine2D (void)
{
   cce__terminateDynamicMap2D();
   free(g_textures);
   glDeleteTextures(1, &glTexturesArray);
   for (struct UsedUBO *iterator = g_UBOs, *end = g_UBOs + g_UBOsQuantity; iterator < end; ++iterator)
   {
      glDeleteBuffers(1, &(iterator->UBO));
   }
   free(g_UBOs);
   free(bufferUniformsOffsets);
   free(uniformLocations);
   free(texturesPath);
   glDeleteProgram(shaderProgram);
   cce__terminateEngine();
}

CCE_PUBLIC_OPTIONS int cceEngine2D (void)
{
   if (map2Dflags & CCE_INIT)
      return -1;
   cce__showWindow();
   cce__engineUpdate();
   GL_CHECK_ERRORS;
   struct Map2Darray *maps = loadMap2DwithDependies(NULL, 0u);
   cce__setCurrentArrayOfMaps(maps);
   uint32_t closestMapPosition;
   int32_t closestMapDistance = 0u, currentDistance;
   while (!(*cce__flags & CCE_ENGINE_STOP))
   {
      cce__processDynamicMap2DElements();
      
      if (map2Dflags & CCE_PROCESS_TEXTURES)
         cce__updateTexturesArray();
      
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      GL_CHECK_ERRORS;
      glBindTexture(GL_TEXTURE_2D_ARRAY, glTexturesArray);
      GL_CHECK_ERRORS;
      if (maps->main->exitMapsQuantity)
      {
         closestMapDistance = INT32_MAX;
         for (struct ExitMap2D *iterator = maps->main->exitMaps, *end = maps->main->exitMaps + maps->main->exitMapsQuantity; iterator < end; ++iterator)
         {
            currentDistance = getMapBorderDistance(iterator);
            if (currentDistance < closestMapDistance)
            {
               closestMapDistance = currentDistance;
               closestMapPosition = iterator - maps->main->exitMaps;
            }
         }
         lastNearestMap2D = *(maps->dependies + closestMapPosition);
         drawMap2Dcommon(maps);
      }
      else
      {
         drawMap2D(maps->main);
      }
      
      glBindVertexArray(g_dynamicMap->VAO);
      GL_CHECK_ERRORS;
      glBindBufferRange(GL_UNIFORM_BUFFER, 1u, (g_UBOs + g_dynamicMap->UBO_ID)->UBO, 0u, g_uniformBufferSize);
      GL_CHECK_ERRORS;
      glDrawArrays(GL_POINTS, 0u, g_dynamicMap->elementsQuantity);
      GL_CHECK_ERRORS;
      cce__swapBuffers();
      cce__engineUpdate();
      processLogicMap2Dcommon(maps);
      processLogicDynamicMap2D(g_dynamicMap, maps->main);

      static uint16_t frames = 0;
      static float timePassed = 0.0f;
      timePassed += *cceDeltaTime;
      ++frames;
      if (timePassed > 2.0f)
      {
         printf("%f FPS\n", frames / timePassed);
         frames = 0;
         timePassed = 0.0f;
      }

      if (closestMapDistance < 0)
      {
         maps = loadMap2DwithDependies(maps, (maps->main->exitMaps + closestMapPosition)->ID);
         cce__setCurrentArrayOfMaps(maps);
      }
   }
   for (struct Map2D **iterator = maps->dependies, **end = maps->dependies + maps->main->exitMapsQuantity; iterator < end; ++iterator)
   {
      cceFreeMap2D(*iterator);
   }
   cceFreeMap2D(maps->main);
   free(maps->dependies);
   free(maps);
   cce__terminateEngine2D();
   return 0;
}
