import os
import traceback
from core import G
import random as rand
import re

def load(app):
    ##temp file name:
    path = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/example1.fbx"
    
    human = G.app.selectedHuman
    varList = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/VariablesList.txt"
    modifiers = []
    
    try:
        with open(varList, 'r') as input:
            for line in input:
                name = line.strip()
                ##the line is empty
                if not name:
                    continue
                ##line is not empty
                else:
                    modifiers.append(name)
                
    except FileNotFoundError:
        print(f"Error: The file '{varList}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")
    ##print(modifiers)
    
    parameter1, parameter2 = -.05, .05
    ##modify values
    findSymmetry(human, modifiers, parameter1, parameter2)
    
    ##export:
    exporter = G.app.getExporter("fbx")
    exporter.export(human, path)

def unload(app):
    pass

#constrain symmetry between matching appendages

def findSymmetry(human, modifiers, parameter1, parameter2):
    #modifiers that must be set within a certain range of one another
    fatMods = ["armslegs/l-lowerarm-fat-decr|incr", "armslegs/l-lowerleg-fat-decr|incr", "armslegs/l-upperarm-fat-decr|incr", "armslegs/l-upperleg-fat-decr|incr", "armslegs/r-lowerarm-fat-decr|incr",
               "armslegs/r-lowerleg-fat-decr|incr", "armslegs/r-upperarm-fat-decr|incr", "armslegs/r-upperleg-fat-decr|incr","head/head-fat-decr|incr",
    ]
    muscleMods = ["armslegs/l-lowerarm-muscle-decr|incr", "armslegs/l-lowerleg-muscle-decr|incr", "armslegs/l-upperarm-muscle-decr|incr", "armslegs/l-upperarm-shoulder-muscle-decr|incr", "armslegs/l-upperleg-muscle-decr|incr",
               "armslegs/r-lowerarm-muscle-decr|incr", "armslegs/r-lowerleg-muscle-decr|incr", "armslegs/r-upperarm-muscle-decr|incr", "armslegs/r-upperarm-shoulder-muscle-decr|incr","armslegs/r-upperleg-muscle-decr|incr",
               "macrodetails-universal/Muscle", "torso/torso-muscle-dorsi-decr|incr", "torso/torso-muscle-pectoral-decr|incr",
               ]
    fat = {}
    muscle = {}
    temp = {}
    noSym = {}
    symSet = {}
    #where everything is stored
    finalSet = {}
    #additional method of sorting, by group names
    groups = []
    #group without orientation
    presets = []
    
    
    #               PRESET VALUES:              #
    
    randFat = rand.uniform(-1, 1)
    fatRangeVal = rand.uniform(.05, .2)
    
    tempFat1 = randFat - fatRangeVal
    tempFat2 = randFat + fatRangeVal
    
    randMuscle = rand.uniform(-1, 1)
    muscleRangeVal = rand.uniform(.05, .2)
    
    tempMuscle1 = randFat - fatRangeVal
    tempMuscle2 = randFat + fatRangeVal
    
    #random value within the appropriate range
    for i in fatMods:
        fat[i] = rand.uniform(tempFat1, tempFat2)
        finalSet[i] = fat[i]
        
    for x in muscleMods:
        muscle[x] = rand.uniform(tempMuscle1, tempMuscle2)
        finalSet[i] = muscle[i]
        
    #find and set height value before any other modifiers:
    modHeight = rand.uniform(parameter1, parameter2)
    noSym["macrodetails-height/Height"] = modHeight
    presets.append("macrodetails-height/Height")
    finalSet["macrodetails-height/Height"] = noSym["macrodetails-height/Height"]
    
    #upper and lower arms
    noSym["measure/measure-upperarm-length-decr|incr"] = (modHeight/2)
    presets.append("measure/measure-upperarm-length-decr|incr")
    finalSet["measure/measure-upperarm-length-decr|incr"] = noSym["measure/measure-upperarm-length-decr|incr"]
    rangeArm = rand.uniform(.52, .55)
    lowerArm = 1 - rangeArm
    noSym["measure/measure-lowerarm-length-decr|incr"] = 1 - rangeArm
    presets.append("measure/measure-lowerarm-length-decr|incr")
    finalSet["measure/measure-lowerarm-length-decr|incr"] = noSym["measure/measure-lowerarm-length-decr|incr"]
    #gender and gender assigned modifiers:
    genRand = rand.uniform(-1, 1)
                    #-1 == female
                    #+1 == male
    noSym["macrodetails/Gender"] = genRand
    finalSet["macrodetails/Gender"] = noSym["macrodetails/Gender"]
    presets.append("macrodetails/Gender")
    if genRand <= 0:
                legLen = rand.uniform(.49, .52)
    elif genRand >= 0:
                legLen = rand.uniform(.48, .51)
    lowLeg = legLen / 2
    noSym["armslegs/lowerlegs-height-decr|incr"] = lowLeg
    presets.append("armslegs/lowerlegs-height-decr|incr")
    finalSet["armslegs/lowerlegs-height-decr|incr"] = noSym["armslegs/lowerlegs-height-decr|incr"]
    noSym["measure/measure-upperleg-height-decr|incr"] = lowLeg
    presets.append("measure/measure-upperleg-height-decr|incr")
    finalSet["measure/measure-upperleg-height-decr|incr"] = noSym["measure/measure-upperleg-height-decr|incr"]
    
    #                                           #
    
    #              ORIENTED VALUES:             #
    
    for modifier in modifiers:
        #removes white space
        line = modifier.strip()
        #different amount of parts for each group
        tempLine = line.split('/')
        groups.append(tempLine[0])
        
        #starting sorting orientation
        nline = tempLine[1].split('-')
        if nline[0] == 'l' or nline[0] == 'r':
            tempOrient = nline[0]
            tempName = tempLine[0] + '/' +nline[0] + '-' + nline[1] + '-' + nline[2] + '-' + nline[3]
            storageName = tempLine[0] + '/' + '_' + '-' + nline[1] + '-' + nline[2] + '-' + nline[3]
            
            
            if tempName in temp:
                #then nothing should happen, this value has already been assigned
                symSet[storageName] = temp[tempName]
                finalSet[storageName] = symSet[storageName]
                continue
                
            #if the modifier has an orientation, and has not already
            #been given a value, and is not apart of the muscle/fat
            #lists, assign a random value
            
    #                                           #
    
    #            NON-ORIENTED VALUES:           #
    
            elif tempName not in temp and line not in fatMods and line not in muscleMods and line not in presets:
                modVal = rand.uniform(parameter1, parameter2)
                temp[tempName] = modVal
                symSet[storageName] = modVal
                finalSet[storageName] = symSet[storageName]
        else:
            mod = rand.uniform(parameter1, parameter2)
            temp[tempName] = modVal
            finalSet[storageName] = temp[tempName]
        
        orient = ['l', 'r']
        count = 0
        for key in symSet:
            tempLine1 = key.split('/')
            nline1 = tempLine[1].split('-')
            finalName = tempLine1[0] + '/' + orient[count] + '-' + nline1[1] + '-' + nline1[2] + '-' + nline1[3]
            finalSet[finalName] = symSet[key]
            count += 1
            if count > 1:
                count == 0
                continue
        
        #assign modifiers
    for key in finalSet:
        try:
            getMod = human.getModifier(key)
            tempValues = finalSet.get(key)
            getMod.setValue(tempValues)
        except KeyError:
            print(key)
            
