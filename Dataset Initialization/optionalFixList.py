def fix():
    varList = "Downloads/VariablesList.txt"
    
    try:
        with open(varList, 'r') as input, open("Downloads/variablesOutput.txt", 'w') as output:
            for line in input:
                newLine = line.rstrip('\n') + ', \n'
                output.write(newLine)
    except FileNotFoundError:
        print(f"Error: The file '{input}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")
    
    print("Test Ran Successfully")