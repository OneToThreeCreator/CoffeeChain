#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "engine_common.h"
#include "engine_common_internal.h"
#include "shader.h"
#include "path_getters.h"
#include "external/stb_image.h"

#include "engine_common_glfw.h"

struct GlobalVariables
{
   uint16_t          plotNumber;             /* 1 short, 65536 values (chain) */
   uint_fast16_t    *globalBools;            /* 4 or 8 bytes -> globalBoolsQuantity * 8 bytes (8 B - 8 KiB), 8 - 65536 values */
   uint_fast16_t    *temporaryBools;         /* 4 or 8 bytes -> (1024 - globalBoolsQuantity) * 8 bytes (8 KiB - 8 B), 65536 - 8 values */
};

uint16_t      g_globalBoolsQuantity;    /* Quantity of 8-byte variables */
static struct GlobalVariables cce_Gvars;

static struct alObjects *AL;

static struct UsedTemporaryBools *g_temporaryBools;
static uint16_t                   g_temporaryBoolsQuantity;
static uint16_t                   g_temporaryBoolsQuantityAllocated;
static uint8_t                    g_flags;

void (*engineUpdate__api) (void);
void (*terminateEngine__api) (void);
struct cce_uvec2 (*getCurrentStep) (void);
void (*toFullscreen) (void);
void (*toWindow) (void);
void (*showWindow) (void);
void (*swapBuffers) (void);
CCE_OPTIONS void (*cce_setWindowParameters) (cce_enum parameter, uint32_t a, uint32_t b);

void callActions (void (**doAction)(void*), uint8_t actionsQuantity, uint32_t *actionIDs, uint32_t *actionArgOffsets, cce_void *actionArgs)
{
   for (cce_ubyte j = 0u; j < actionsQuantity; ++j)
   {
      (**(doAction + (*(actionIDs + j))))((void*) (actionArgs + (*(actionArgOffsets + j))));
   }
}

CCE_OPTIONS uint8_t isTimerEnded (struct Timer *timer)
{
   if (timer->initTime == 0.0)
      return 0u;
   return (timer->initTime + timer->delay) <= *cce_currentTime;
}

CCE_OPTIONS void startTimer (struct Timer *timer)
{
   timer->initTime = *cce_currentTime;
}

CCE_OPTIONS uint8_t getBool (uint16_t boolID)
{
   uint_fast16_t *boolean;
   if (boolID < g_globalBoolsQuantity)
   {
      boolean = cce_Gvars.globalBools;
   }
   else
   {
      boolean = cce_Gvars.temporaryBools;
   }
   boolean += ((boolID) >> (3u + SHIFT_OF_FAST_SIZE));
   return ((*boolean) & (((uint_fast16_t) 0x0001u) << ((boolID) & BITWIZE_AND_OF_FAST_SIZE))) > 0u;
}

CCE_OPTIONS void setBool (uint16_t boolID, cce_enum action)
{
   uint_fast16_t *boolean;
   if (boolID < g_globalBoolsQuantity)
   {
      boolean = cce_Gvars.globalBools;
   }
   else
   {
      boolean = cce_Gvars.temporaryBools;
   }
   boolean += ((boolID) >> (3u + SHIFT_OF_FAST_SIZE));
   register uint_fast16_t mask = (((uint_fast16_t) 0x0001u) << ((boolID) & BITWIZE_AND_OF_FAST_SIZE));
   switch (action)
   {
      case CCE_ENABLE_BOOL:
      {
         (*boolean) |=  mask;
         break;
      }
      case CCE_DISABLE_BOOL:
      {
         (*boolean) &= ~mask;
         break;
      }
      case CCE_SWITCH_BOOL:
      {
         (*boolean) ^=  mask;
         break;
      }
   }
}

CCE_OPTIONS uint8_t checkPlotNumber (uint16_t value)
{
   return cce_Gvars.plotNumber > value;
}

CCE_OPTIONS void increasePlotNumber (uint16_t value)
{
   cce_Gvars.plotNumber += value;
}

CCE_OPTIONS void setPlotNumber      (uint16_t value)
{
   cce_Gvars.plotNumber = value;
}

static void updateTemporaryBoolsArray (void)
{
   for (struct UsedTemporaryBools *iterator = g_temporaryBools, *end = g_temporaryBools + g_temporaryBoolsQuantity; iterator < end; ++iterator)
   {
      if (iterator->flags & 0x4)
      {
         iterator->flags &= 0x1;
         memset(iterator->temporaryBools, 0, (UINT16_MAX - g_globalBoolsQuantity + 1u) >> 3u);
      }
   }
}

uint16_t getFreeTemporaryBools (void)
{
   g_flags |= CCE_PROCESS_TEMPORARY_BOOLS_ARRAY;
   for (struct UsedTemporaryBools *iterator = g_temporaryBools, *end = g_temporaryBools + g_temporaryBoolsQuantity; iterator < end; ++iterator)
   {
      if (iterator->flags & 0x1)
         continue;
      iterator->flags |= 0x1;
      return (uint16_t) (iterator - g_temporaryBools);
   }
   if (g_temporaryBoolsQuantity >= g_temporaryBoolsQuantityAllocated)
   {
      g_temporaryBoolsQuantityAllocated += CCE_ALLOCATION_STEP;
      g_temporaryBools = realloc(g_temporaryBools, g_temporaryBoolsQuantityAllocated * sizeof(struct UsedTemporaryBools));
      memset(g_temporaryBools + g_temporaryBoolsQuantityAllocated - CCE_ALLOCATION_STEP, 0u, CCE_ALLOCATION_STEP);
   }
   struct UsedTemporaryBools *temporaryBools = g_temporaryBools + g_temporaryBoolsQuantity;
   temporaryBools->flags |= 0x1;
   if (!(temporaryBools->temporaryBools))
   {
      temporaryBools->temporaryBools = (uint_fast16_t *) calloc(((UINT16_MAX + (uint32_t) 1u) >> (SHIFT_OF_FAST_SIZE + 3u)) - (g_globalBoolsQuantity >> (SHIFT_OF_FAST_SIZE + 3u)), sizeof(uint_fast16_t));
   }
   return g_temporaryBoolsQuantity++;
}

void releaseTemporaryBools (uint16_t ID)
{
   (g_temporaryBools + ID)->flags = 0x4;
   return;
}

void releaseUnusedTemporaryBools (uint16_t ID)
{
   (g_temporaryBools + ID)->flags = 0x0;
   return;
}

void setCurrentTemporaryBools (uint16_t temporaryBoolsID)
{
   cce_Gvars.temporaryBools = (g_temporaryBools + temporaryBoolsID)->temporaryBools;
}

void processLogic (uint32_t logicQuantity, struct ElementLogic *logic, struct Timer *timers, void (**doAction)(void*),
                   cce_ubyte (*fourth_if_func)(uint16_t, va_list), ...)
{
   va_list argp, argcp;
   va_start(argp, fourth_if_func);
   uint_fast32_t boolSum;
   uint16_t boolNumber;
   struct ElementLogic *endLogic = (logic + logicQuantity);
   uint_fast16_t currentOperations;
   cce_byte isLogic;
   while (logic < endLogic)
   {
      boolSum = 0u;
      currentOperations = (*logic->operations);
      for (cce_byte j = logic->logicElementsQuantity - 1u;; --j)
      {
         boolNumber = (*((logic->logicElements) + j));
         switch (((logic->elementType) >> (j * 2u)) & 0x3)
         {
            case CCE_GLOBAL_BOOL_LOGIC_ELEMENT: 
            {
               boolSum += ((uint_fast16_t) getBool(boolNumber) << j);
               break;
            }
            case CCE_PLOT_NUMBER_LOGIC_ELEMENT:
            {
               boolSum += (((uint_fast16_t) (cce_Gvars.plotNumber > boolNumber)) << j);
               break;
            }
            case CCE_TIMER_LOGIC_ELEMENT:
            {
               boolSum += ((uint_fast16_t) isTimerEnded(timers + boolNumber) << j);
               break;
            }
            default:
            {
               va_copy(argcp, argp);
               boolSum += ((uint_fast16_t) fourth_if_func(boolNumber, argcp) << j); // x << j == x * pow(2, j)
               va_end(argcp);
            }
         }
         // Some dark portable (probably) magic for checking results and exit earlier if only false or only true is possible (and no other options).
         switch (j)
         {
            #if UINT_FAST16_MAX < UINT64_MAX
            case 6u:
            #if UINT_FAST16_MAX < UINT32_MAX
            case 5u:
            #endif // UINT_FAST16_MAX < UINT32_MAX
            {
               cce_byte state = 0;
               for (uint_fast16_t *iterator = (logic->operations + (boolSum >> SHIFT_OF_FAST_SIZE),
               *end = (logic->operations + (boolSum >> SHIFT_OF_FAST_SIZE) + (1 << (j - 3u))/sizeof(uint_fast16_t); iterator < end; ++iterator)
               {
                  if (!(*iterator))
                  {
                     --state;
                  }
                  else if ((*iterator) == UINT_FAST16_MAX)
                  {
                     ++state;
                  }
               }
               switch (state)
               {
                  case -sizeof(uint_fast16_t):
                  {
                     isLogic = 0u;
                     break;
                  }
                  case sizeof(uint_fast16_t):
                  {
                     isLogic = 1u;
                     break;
                  }
                  default:
                  {
                     continue;
                  }
               }
               break;
            }
            #if UINT_FAST16_MAX == UINT32_MAX
            case 5u:
            #else
            case 4u:
            #endif // UINT_FAST16_MAX == UINT32_MAX
            #else
            case 6u:
            #endif // UINT_FAST16_MAX < UINT64_MAX
            {
               currentOperations = (*(logic->operations + (boolSum >> SHIFT_OF_FAST_SIZE)));
               switch (currentOperations)
               {
                  case 0u:
                  {
                     isLogic = 0u;
                     break;
                  }
                  case UINT_FAST16_MAX:
                  {
                     isLogic = 1u;
                     break;
                  }
                  default:
                  {
                     continue;
                  }
               }
               break;
            }
            #if UINT_FAST16_MAX > UINT32_MAX
            case 5u:
            #endif
            #if UINT_FAST16_MAX > UINT16_MAX
            case 4u:
            #endif
            case 3u:
            case 2u:
            case 1u:
            case 0u:
            {
               uint_fast16_t mask = ((1u << (1u << j)) - 1u) << (boolSum & BITWIZE_AND_OF_FAST_SIZE);
               currentOperations &= mask;
               if (!currentOperations)
               {
                  isLogic = 0u;
                  break;
               }
               else if (currentOperations == mask)
               {
                  isLogic = 1u;
                  break;
               }
               continue;
            }
            default:
            {
               continue;
            }
         }
         break;
      }
      if (isLogic)
      {
         callActions(doAction, logic->actionsQuantity, logic->actionIDs, logic->actionsArgOffsets, logic->actionsArg);
      }
      ++logic;
   }
   va_end(argp);
}

cce_ubyte checkCollision (int32_t element1_x, int32_t element1_y, int32_t element1_width, int32_t element1_height,
                          int32_t element2_x, int32_t element2_y, int32_t element2_width, int32_t element2_height)
{
   if (element1_x < element2_x)
   {
      if (element1_x + element1_width < element2_x)
      {
         return 0u;
      }
   }
   else if (element1_x > element2_x + element2_width)
   {
      return 0u;
   }
   
   if (element1_y < element2_y)
   {
      if (element1_y + element1_height < element2_y)
      {
         return 0u;
      }
   }
   else if (element1_y > element2_y + element2_height)
   { 
      return 0u;
   }
   return 1u;
}

struct ElementGroup* loadGroups (uint16_t groupsQuantity, FILE *map_f)
{
   if (!groupsQuantity)
   {
      return NULL;
   }
   struct ElementGroup *groups = (struct ElementGroup*) malloc(groupsQuantity * sizeof(struct ElementGroup));
   for (struct ElementGroup *iterator = groups, *end = (groups + groupsQuantity); iterator < end; ++iterator)
   {
      fread(&(iterator->elementsQuantity), 2u/*uint16_t*/, 1u, map_f);
      if (iterator->elementsQuantity)
      {
         iterator->elementIDs = (uint32_t*) malloc(iterator->elementsQuantity * sizeof(uint32_t));
         fread(iterator->elementIDs, 4u/*uint32_t*/, iterator->elementsQuantity, map_f);
      }
      else
      {
         iterator->elementIDs = NULL;
      }
   }
   return groups;
}

void writeGroups (uint16_t groupsQuantity, struct ElementGroup *groups, FILE *map_f)
{
   for (struct ElementGroup *iterator = groups, *end = (groups + groupsQuantity); iterator < end; ++iterator)
   {
      fwrite(&(iterator->elementsQuantity), 2u/*uint16_t*/, 1u, map_f);
      if (iterator->elementsQuantity)
      {
         fwrite(iterator->elementIDs, 4u/*uint32_t*/, iterator->elementsQuantity, map_f);
      }
   }
}

struct ElementLogic* loadLogic (uint8_t logicQuantity, FILE *map_f)
{
   if (!logicQuantity)
   {
      return NULL;
   }
   struct ElementLogic *logic = (struct ElementLogic*) malloc(logicQuantity * sizeof(struct ElementLogic));
   struct ElementLogic *end = (logic + logicQuantity);

   uint_fast32_t operationsQuantityInBytes;
   uint8_t isLogicQuantityHigherThanThree;
   for (struct ElementLogic *iterator = logic; iterator < end; ++iterator)
   {
      fread(&(iterator->logicElementsQuantity), 1u/*uint8_t*/,   1u,                                                        map_f);
      (iterator->logicElements) = (uint16_t *) malloc((iterator->logicElementsQuantity) * sizeof(uint16_t));
      fread( (iterator->logicElements),         2u/*uint16_t*/,  (iterator->logicElementsQuantity),                         map_f);
      
      isLogicQuantityHigherThanThree = iterator->logicElementsQuantity > 3u;
      operationsQuantityInBytes = (0x01 << ((iterator->logicElementsQuantity) - 3u) * isLogicQuantityHigherThanThree) + (!isLogicQuantityHigherThanThree);
      (iterator->operations) = (uint_fast16_t*) calloc((operationsQuantityInBytes > sizeof(uint_fast16_t)) ? operationsQuantityInBytes : sizeof(uint_fast16_t), 1u);
      if (operationsQuantityInBytes > sizeof(uint_fast16_t))
      {
         fread( (iterator->operations),         sizeof(uint_fast16_t),     operationsQuantityInBytes >> SHIFT_OF_FAST_SIZE, map_f);
      }
      else
      {
         fread(iterator->operations,            operationsQuantityInBytes, 1u,                                              map_f);
      }
      fread(&(iterator->elementType),           8u/*uint64_t*/,  1u,                                                        map_f);
      fread(&(iterator->actionsQuantity),       1u/*uint8_t*/,   1u,                                                        map_f);
      (iterator->actionIDs) = (uint32_t *) malloc((iterator->actionsQuantity) * sizeof(uint32_t));
      fread( (iterator->actionIDs),             4u/*uint32_t*/,  (iterator->actionsQuantity),                               map_f);
      (iterator->actionsArgOffsets) = (uint32_t *) malloc((iterator->actionsQuantity + 1u) * sizeof(uint32_t));
      *(iterator->actionsArgOffsets) = 0u;
      fread( (iterator->actionsArgOffsets + 1), 4u/*uint32_t*/,  (iterator->actionsQuantity),                               map_f);
      (iterator->actionsArg) = (cce_void *) malloc(*(iterator->actionsArgOffsets + iterator->actionsQuantity)/* sizeof(cce_void)*/);
      fread( (iterator->actionsArg),            1u/*cce_void*/, *(iterator->actionsArgOffsets + iterator->actionsQuantity), map_f);
   }
   return logic;
}

void writeLogic (uint8_t logicQuantity, struct ElementLogic *logic, FILE *map_f)
{
   struct ElementLogic *end = (logic + logicQuantity - 1u);
   uint_fast32_t operationsQuantityInBytes;
   uint8_t isLogicQuantityHigherThanThree;
   for (struct ElementLogic *iterator = logic; iterator <= end; ++iterator)
   {
      fwrite(&(iterator->logicElementsQuantity), 1u/*uint8_t*/,   1u,                                                        map_f);
      fwrite( (iterator->logicElements),         2u/*uint16_t*/,  (iterator->logicElementsQuantity),                         map_f);
      
      isLogicQuantityHigherThanThree = iterator->logicElementsQuantity > 3u;
      operationsQuantityInBytes = (0x01 << ((iterator->logicElementsQuantity) - 3u) * isLogicQuantityHigherThanThree) + (!isLogicQuantityHigherThanThree);
      if (operationsQuantityInBytes > sizeof(uint_fast16_t))
      {
         fwrite( (iterator->operations),         sizeof(uint_fast16_t), operationsQuantityInBytes >> SHIFT_OF_FAST_SIZE,     map_f);
      }
      else
      {
         fwrite(iterator->operations,            operationsQuantityInBytes, 1u,                                              map_f);
      }
      fwrite(&(iterator->elementType),           8u/*uint64_t*/,  1u,                                                        map_f);
      fwrite(&(iterator->actionsQuantity),       1u/*uint8_t*/,   1u,                                                        map_f);
      fwrite( (iterator->actionIDs),             4u/*uint32_t*/,  (iterator->actionsQuantity),                               map_f);
      fwrite( (iterator->actionsArgOffsets + 1), 4u/*uint32_t*/,  (iterator->actionsQuantity),                               map_f);
      fwrite( (iterator->actionsArg),            1u/*cce_void*/, *(iterator->actionsArgOffsets + iterator->actionsQuantity), map_f);
   }
}

CCE_OPTIONS size_t binarySearch (const void *const array, const size_t arraySize, const size_t typeSize, const size_t step, const size_t value)
{
   if (!arraySize)
      return -1;
      
   const uint8_t *iterator = (uint8_t*) array;
   const uint8_t *end = ((uint8_t*) array) + arraySize * step;
   size_t remain = arraySize;
   size_t typeRemain;
   size_t typeMask;
   if (typeSize >= sizeof(size_t))
   {
      typeMask = SIZE_MAX;
   }
   else
   {
   typeMask = (((size_t) 1u) << typeSize * 8u) - 1u;
   }
   do
   {
      remain >>= 1u;
      typeRemain = remain * step;
      if (iterator + typeRemain + step >= end)
         continue;
      iterator += (typeRemain + step) * (((*((size_t*) (iterator + typeRemain))) & typeMask) < value);
   }
   while (remain > 0u); /* Checking for last valid value, because we cannot detect underflow of unsigned variable */
   return (iterator - (uint8_t*) array) / step;
}

struct operationsStack
{
   uint_fast16_t *operations;
   struct operationsStack *prev;
   uint_fast16_t operationPriority;
   uint8_t flags; /* 0x1 - logicElement is inverted */
   uint8_t operation;
   uint8_t logicElementID;
};

/* Logic operations */
#define NEG    1u
#define AND    2u
#define NAND   3u
#define OR     4u
#define NOR    5u
#define XOR    6u
#define XNOR   7u
#define GRTR   8u
#define GRTREQ 9u
#define LESS   10u
#define LESSEQ 11u
#define IMPL   LESSEQ

static struct operationsStack* pushToOperationsStack (struct operationsStack *restrict stack, uint_fast32_t priority, uint8_t operation, uint_fast16_t *operations)
{
   struct operationsStack *currentStack = (struct operationsStack*) malloc(sizeof(struct operationsStack));
   currentStack->operationPriority = priority;
   currentStack->operations = operations;
   currentStack->flags = operations > 0u;
   currentStack->operation = operation;
   currentStack->prev = stack;
   return currentStack;
}

/*
struct operationsLoaded
{
   uint_fast16_t *operations;
   uint8_t        isBusy;
};

static struct operationsLoaded *operationsLoaded;
static uint16_t                 operationsLoadedQuantity;

static uint16_t getFreeOperations (uint8_t operationsQuantity)
{
   toBeImplemented (or not, idk)
}
*/

static uint_fast16_t* generateOperationsFromLogicElement (uint8_t ID, uint8_t isInverted, uint8_t logicElementsQuantity)
{
   uint8_t isLogicQuantityHigherThanVariableSize = logicElementsQuantity > (3u + SHIFT_OF_FAST_SIZE);
   size_t operationsQuantity = (0x01 << (logicElementsQuantity - (3u + SHIFT_OF_FAST_SIZE))) * isLogicQuantityHigherThanVariableSize + (!isLogicQuantityHigherThanVariableSize);
   uint_fast16_t *operations = calloc(operationsQuantity, sizeof(uint_fast16_t));
   uint_fast16_t step;
   if ((logicElementsQuantity - ID) < (3u + SHIFT_OF_FAST_SIZE)) 
   {
      step = 1u << (logicElementsQuantity - ID - 1u);
      for (uint_fast16_t mask = (UINT_FAST16_MAX >> (sizeof(uint_fast16_t) * 8u - step)) << (!isInverted * step);; mask <<= step * 2u)
      {
         *operations |= mask;
         /* Checking for last valid value, because shifting on value that is greater then or equal to variable size is *undefined behavior**/
         if ((mask & (((uint_fast16_t) 0x1) << (BITWIZE_AND_OF_FAST_SIZE - step * (isInverted > 0u)))))
            break;
      }
      
      for (uint_fast16_t *iterator = operations + 1u, *end = operations + operationsQuantity; iterator < end; ++iterator)
      {
         *iterator = *operations;
      }
   }
   else
   {
      step = 1u << (logicElementsQuantity - ID - (3u + SHIFT_OF_FAST_SIZE));
      for (uint_fast16_t *iterator = operations + (!isInverted * step), *end = operations + operationsQuantity; iterator < end; iterator += step * 2u)
      {
         memset(iterator, 1u, step * sizeof(uint_fast16_t));
      }
   }
   return operations;
}

static struct operationsStack* computeStackDownToPriority (uint_fast32_t priority, struct operationsStack *stack, uint8_t logicQuantity)
{
   uint8_t isLogicQuantityHigherThanVariableSize = logicQuantity > (3u + SHIFT_OF_FAST_SIZE);
   size_t operationsQuantity = (0x01 << (logicQuantity - (3u + SHIFT_OF_FAST_SIZE))) * isLogicQuantityHigherThanVariableSize + (!isLogicQuantityHigherThanVariableSize);
   uint_fast16_t *operations, *prevOperations, *end;
   struct operationsStack *iterator = stack;
   
   if (iterator->operation == 0u)
   {
      if (!iterator->operations)
      {
         iterator->operations = generateOperationsFromLogicElement(iterator->logicElementID, iterator->flags & 1u, logicQuantity);
      }
      return iterator;
   }
   
   for (struct operationsStack *prev; iterator->operation != 0u;)
   {
      if (iterator->operationPriority < priority)
      {
         return iterator;
      }
      prev = iterator->prev;
      
      if (!iterator->operations)
      {
         iterator->operations = generateOperationsFromLogicElement(iterator->logicElementID, iterator->flags & 1u, logicQuantity);
      }
      operations = iterator->operations;
      
      end = operations + operationsQuantity;
      if (iterator->operation != NEG)
      {
         if (!prev->operations)
         {
            prev->operations = generateOperationsFromLogicElement(prev->logicElementID, prev->flags & 1u, logicQuantity);
         }
         prevOperations = prev->operations;
      }
      
      switch (iterator->operation)
      {
         case NEG:
         {
            if (prev->operations)
               free(prev->operations);
            prev->operations = operations;
            while (operations < end)
            {
               (*operations) = ~(*operations);
               ++operations;
            }
            free(iterator);
            iterator = prev;
            continue;
         }
         case AND:
         {
            while (operations < end)
            {
               (*prevOperations) &= (*operations);
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case NAND:
         {
            while (operations < end)
            {
               (*prevOperations) = ~((*prevOperations) & (*operations));
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case OR:
         {
            while (operations < end)
            {
               (*prevOperations) |= (*operations);
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case NOR:
         {
            while (operations < end)
            {
               (*prevOperations) = ~((*prevOperations) | (*operations));
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case XOR:
         {
            while (operations < end)
            {
               (*prevOperations) ^= (*operations);
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case XNOR:
         {
            while (operations < end)
            {
               (*prevOperations) = ~((*prevOperations) ^ (*operations));
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case GRTR:
         {
            while (operations < end)
            {
               (*prevOperations) &= ~(*operations);
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case GRTREQ:
         {
            while (operations < end)
            {
               (*prevOperations) |= (~(*operations));
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case LESS:
         {
            while (operations < end)
            {
               (*prevOperations) = (~(*prevOperations)) & (*operations);
               ++operations;
               ++prevOperations;
            }
            break;
         }
         case LESSEQ:
//       case IMPL:
         {
            while (operations < end)
            {
               (*prevOperations) = (~(*prevOperations)) | (*operations);
               ++operations;
               ++prevOperations;
            }
            break;
         }
      }
      free(iterator->operations);
      free(iterator);
      iterator = prev;
   }
   return iterator;
}

static int compare (const void *a, const void *b)
{
   const char char_a = *((char*) a);
   const char char_b = *((char*) b);
   return (char_a > char_b) - (char_a < char_b);
}

/* Parsing string to truth table. Has hardcoced limit to 32 elements (already 512MiB size), bigger amount could not fit into memory while parsing */
CCE_OPTIONS uint_fast16_t* parseStringToLogicOperations (const char *const string, uint_fast8_t *const logicQuantity)
{
   char dictionary[33] = ""; //last is '\0'
   uint8_t dictionarySize = 0u;
   size_t stringSize = 0u;
   for (const char *iterator = string; *iterator != '\0'; ++iterator, ++stringSize)
   {
      if ((*iterator >= '0' && *iterator <= '9') || (*iterator >= 'A' && *iterator <= 'Z') || (*iterator >= 'a' && *iterator <= 'z'))
      {
         for (char *jiterator = dictionary; *iterator != *jiterator; ++jiterator)
         {
            if (*jiterator == '\0')
            {
               if ((dictionary + 33) == jiterator) return NULL;
               *jiterator = *iterator;
               *(jiterator + 1u) = '\0';
               ++dictionarySize;
               break;
            }
         }
      }
   }
   qsort(dictionary, dictionarySize, sizeof(char), compare);
   
   struct operationsStack *stack = (struct operationsStack*) malloc(sizeof(struct operationsStack));
   stack->operation = 0u;
   stack->flags = 0u;
   stack->operations = NULL;
   stack->operationPriority = 0u;
   stack->prev = NULL;
   uint_fast32_t currentPriority, lastPriority = 0u; /* 8 - (), 4 - !, 3 - &, 2 - |, 1 - ^, 1 - >, 1 - <, 1 - = */
   uint_fast16_t brackets = 0u;
   uint8_t isInverted = 0u;
   for (const char *iterator = string; *iterator != '\0'; ++iterator)
   {
      switch (*iterator)
      {
         case '(':
         {
            if (isInverted)
            {
               currentPriority = 4u + brackets * 8;
               stack = pushToOperationsStack(stack, currentPriority, NEG, NULL);
               lastPriority = currentPriority;
               isInverted = 0u;
            }
            ++brackets;
            break;
         }
         case ')':
         {
            --brackets;
            break;
         }
         case '!':
         case '~':
         {
            isInverted ^= 1u;
            break;
         }
         case '&':
         case '*':
         {
            if (*(iterator - 1u) == *iterator)
               continue;
            currentPriority = 3u + brackets * 8;
            if (lastPriority >= currentPriority)
            {
               stack = computeStackDownToPriority(currentPriority, stack, dictionarySize);
            }
            stack = pushToOperationsStack(stack, currentPriority, AND + isInverted, NULL);
            isInverted = 0u;
            lastPriority = currentPriority;
            break;
         }
         case '|':
         case '+':
         {
            if (*(iterator - 1u) == *iterator)
               continue;
            currentPriority = 2u + brackets * 8;
            if (lastPriority >= currentPriority)
            {
               stack = computeStackDownToPriority(currentPriority, stack, dictionarySize);
            }
            stack = pushToOperationsStack(stack, currentPriority, OR + isInverted, NULL);
            isInverted = 0u;
            lastPriority = currentPriority;
            break;
         }
         case '^':
         {
            currentPriority = 1u + brackets * 8;
            if (lastPriority >= currentPriority)
            {
               stack = computeStackDownToPriority(currentPriority, stack, dictionarySize);
            }
            stack = pushToOperationsStack(stack, currentPriority, XOR + isInverted, NULL);
            isInverted = 0u;
            lastPriority = currentPriority;
            break;
         }
         case '>':
         {
            if (*(iterator - 1u) == '-' || *(iterator - 1u) == '=')
            {
               stack->operation = IMPL;
               continue;
            }
            currentPriority = 1u + brackets * 8;
            if (lastPriority >= currentPriority)
            {
               stack = computeStackDownToPriority(currentPriority, stack, dictionarySize);
            }
            stack = pushToOperationsStack(stack, currentPriority, GRTR, NULL);
            lastPriority = currentPriority;
            break;
         }
         case '<':
         {
            currentPriority = 1u + brackets * 8;
            if (lastPriority >= currentPriority)
            {
               stack = computeStackDownToPriority(currentPriority, stack, dictionarySize);
            }
            stack = pushToOperationsStack(stack, currentPriority, LESS, NULL);
            lastPriority = currentPriority;
            break;
         }
         case '=':
         {
            if (*(iterator - 1u) == *iterator)
               continue;
            
            if (*(iterator - 1u) == '>' || *(iterator - 1u) == '<')
            {
               ++(stack->operation);
               continue;
            }
            currentPriority = 1u + brackets * 8;
            if (lastPriority >= currentPriority)
            {
               stack = computeStackDownToPriority(currentPriority, stack, dictionarySize);
            }
            stack = pushToOperationsStack(stack, currentPriority, XNOR - isInverted, NULL);
            isInverted = 0u;
            lastPriority = currentPriority;
            break;
         }
         default:
         {
            if ((*iterator >= '0' && *iterator <= '9') || (*iterator >= 'A' && *iterator <= 'Z') || (*iterator >= 'a' && *iterator <= 'z'))
            {
               stack->logicElementID = binarySearch(dictionary, dictionarySize, sizeof(char), sizeof(char), (*iterator));
               stack->flags |= isInverted;
               isInverted = 0u;
            }
         }
      }
   }
   stack = computeStackDownToPriority(0u, stack, dictionarySize);
   uint_fast16_t *operations = stack->operations;
   free(stack);
   if (logicQuantity)
   {
      *logicQuantity = dictionarySize;
   }
   return operations;
}

int initEngine (const char *label, uint16_t globalBoolsQuantity)
{
   // We have only one api yet
   if (initEngine__glfw(label, globalBoolsQuantity) != 0)
      return -1;
      
   g_temporaryBools = (struct UsedTemporaryBools*) calloc(CCE_ALLOCATION_STEP, sizeof(struct UsedTemporaryBools));
   g_temporaryBoolsQuantity = 0u;
   g_temporaryBoolsQuantityAllocated = CCE_ALLOCATION_STEP;
   g_globalBoolsQuantity = globalBoolsQuantity;
   cce_Gvars.globalBools  = (uint_fast16_t*) calloc((globalBoolsQuantity >> SHIFT_OF_FAST_SIZE) + ((globalBoolsQuantity & BITWIZE_AND_OF_FAST_SIZE) > 0u), sizeof(uint_fast16_t));
   return 0;
}

void engineUpdate (void)
{
   engineUpdate__api();
   if (g_flags & CCE_PROCESS_TEMPORARY_BOOLS_ARRAY)
   {
      updateTemporaryBoolsArray();
   }
}

void terminateEngine (void)
{
   //stopAL(AL);
   for (struct UsedTemporaryBools *iterator = g_temporaryBools, *end = g_temporaryBools + g_temporaryBoolsQuantity; iterator < end; ++iterator)
   {
      free(iterator->temporaryBools);
   }
   free(g_temporaryBools);
   terminateEngine__api();
   terminateTemporaryDirectory();
}

void doNothing (void)
{
   return;
}
