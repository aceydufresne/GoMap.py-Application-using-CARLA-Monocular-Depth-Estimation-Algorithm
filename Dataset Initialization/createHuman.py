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
    #sort vals into sub-groups, sub groups get a rand val
    symmetry = []
    #rand val per index
    noSymGroup = []
    noSym = {}
    groups = []
    temp = {}
    symSet = {}
    lowerArm = 0
    legLen = 0
    
    faceDev = {}
    fat = {}
    muscle = {}
    
    fatMods = ["armslegs/l-lowerarm-fat-decr|incr", "armslegs/l-lowerleg-fat-decr|incr", "armslegs/l-upperarm-fat-decr|incr", "armslegs/l-upperleg-fat-decr|incr", "armslegs/r-lowerarm-fat-decr|incr",
               "armslegs/r-lowerleg-fat-decr|incr", "armslegs/r-upperarm-fat-decr|incr", "armslegs/r-upperleg-fat-decr|incr","head/head-fat-decr|incr,",
    ]
    muscleMods = ["armslegs/l-lowerarm-muscle-decr|incr", "armslegs/l-lowerleg-muscle-decr|incr", "armslegs/l-upperarm-muscle-decr|incr", "armslegs/l-upperarm-shoulder-muscle-decr|incr", "armslegs/l-upperleg-muscle-decr|incr",
               "armslegs/r-lowerarm-muscle-decr|incr", "armslegs/r-lowerleg-muscle-decr|incr", "armslegs/r-upperarm-muscle-decr|incr", "armslegs/r-upperarm-shoulder-muscle-decr|incr","armslegs/r-upperleg-muscle-decr|incr",
               "macrodetails-universal/Muscle", "torso/torso-muscle-dorsi-decr|incr", "torso/torso-muscle-pectoral-decr|incr",
               ]
    
    
    randFat = rand.uniform(-1, 1)
    fatRangeVal = rand.uniform(.05, .2)
    
    temp1 = 0 - fatRangeVal
    temp2 = 0 + fatRangeVal
    
    for i in fatMods:
        fat[i] = rand.uniform(temp1, temp2)
        
    for x in muscleMods:
        muscle[x] = rand.uniform(temp1, temp2)
        
        #                        Non-Oriented                                    #


    for modifier in modifiers:
        #removes white space
        line = modifier.strip()
        #different amount of parts for each group
        tempLine = line.split('/')
        groups.append(tempLine[0])
        #list of different groups
        nline = tempLine[1].split('-')
        if nline[0] == 'l' or nline[0] == 'r':
            #dropping the orientation, ie: 'l', or 'r'
            tempName = tempLine[0] + '/' + '-' + nline[1] + '-' + nline[2] + '-' + nline[3]
            
            if tempName in temp:
                continue
            elif tempName not in temp:
                modVal = rand.uniform(parameter1, parameter2)
                temp[tempName] = modVal
            
        else:
            noSymGroup.append(tempLine[0] + '/' + tempLine[1])
            #find total height first:
            
            modVal1 = rand.uniform(parameter1, parameter2)
            noSym["macrodetails-height/Height"] = modVal1
            for k in noSymGroup:
                if k == "measure/measure-upperarm-length-decr|incr":
                    temp = noSym["macrodetails-height/Height"]
                    rangeArm = rand.uniform(.52, .55)
                    #some random value between 52% and 55%
                    modVal = (temp/2)
                    lowerArm = 1 - rangeArm
                    
                elif k == "macrodetails-height/Height":
                    #total height
                    noSym["macrodetails-height/Height"] = rand.uniform(parameter1, parameter2)
                    #should be some value between -1, 1, corresponding to a height between 4.9 - 8.2
                
                elif k == "measure/measure-lowerarm-length-decr|incr":
                    modVal = lowerArm
                
                elif k == "macrodetails/Gender":
                    genRand = rand.uniform(-1, 1)
                    #-1 == female
                    #+1 == male
                    if genRand <= 0:
                        legLen = rand.uniform(.49, .52)
                    elif genRand >= 0:
                        legLen = rand.uniform(.48, .51)
                    modVal = genRand
                
                elif k== "armslegs/lowerlegs-height-decr|incr":
                    lowLeg = legLen / 2
                    modVal = lowLeg
                
                elif k == "measure/measure-upperleg-height-decr|incr":
                    upLeg = legLen / 2
                    modVal = upLeg
                
                elif k in fatMods:
                    modVal = fat[k]
                    continue
                elif k in muscleMods:
                    modVal = muscle[k]
                
                else:
                    modVal = rand.uniform(parameter1, parameter2)
                    continue
                
                noSym[k] = modVal
    
    #                        Oriented                                    #
    
    count = 0
    orient = ['l', 'r']
    for i in temp:
        line = i.strip()
        tempLine = line.split('/')
        group = tempLine[0]
        for x in orient:
            tempFinal = group + '/' + x + tempLine[1]
            symSet[tempFinal] = temp[i] #??
    
    for key in symSet:
        try:
            getMod = human.getModifier(key)
            tempValues = symSet.get(key)
            getMod.setValue(tempValues)
        except KeyError:
            print(key)
    
    for key in noSym:
        getMod = human.getModifier(key)
        tempValues = noSym.get(key)
        getMod.setValue(tempValues)
