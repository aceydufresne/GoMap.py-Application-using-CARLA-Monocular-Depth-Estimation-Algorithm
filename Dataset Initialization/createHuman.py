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
    
    modVarVals = ['faceDev', 'fat', 'muscle', 'devicciRatio']
    for i in modVarVals:
        tempRange = rand.random()
    varValMap = {}
    
    

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
            for k in noSymGroup:
                modVal = rand.uniform(parameter1, parameter2)
                noSym[k] = modVal
    
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
        
    