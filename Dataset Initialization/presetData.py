import objaverse.xl as oxl
import pandas as pd
import os


def sketchFabSelector(descriptions):
    #limiting only to the set which has no description values
    sketchFab = descriptions[descriptions["source"]=="sketchfab"]
    csvPath = r"F:\CARLA set\meta_data\sketchfab_selected.csv"
    downloadPath = r"F:\CARLA set\Sketchfab"
    #keep track of what is in the folder already
    target = 1000
    
    if os.path.exists(csvPath):
        #if we have already run the script before.
        previous = pd.read_csv(csvPath)
        #sha256 is all the byte data, 
        previousVals = set(previous["sha256"])
        sketchFab = sketchFab[~sketchFab["sha256"].isin(previousVals)]
    else:
        #we have not run the script before/first test
        previous = pd.DataFrame()
        #a uniform sample with no partitions
    sample = sketchFab.sample(n=min(target, len(sketchFab)), random_state = 42)   
    
    if not previous.empty:
        #if something is already populated in the folder, we add to it instead of creating new hash data
        allSelected = pd.concat([previous,sample], ignore_index = True)
        
    else:
        #the set becomes the initial random seed
        allSelected = sample
        #sanity check, remove all the matching cases of byte data
    allSelected = allSelected.drop_duplicates(subset = "sha256")
    allSelected.to_csv(csvPath, index = False)
    oxl.download_objects(objects=sample, download_dir=downloadPath)
    


if __name__ == "__main__":
    directory = r"F:\CARLA set\meta_data"
#this is a file containing the descriptions and notes for categories of models
    descriptions = oxl.get_annotations(download_dir= directory)
    sources = descriptions["source"].unique()
    sketchFabSelector(descriptions)
