std::vector<funcData> drawLine(const std::vector<EdgeGraph>& graphs, const imageData& image){
    //store all final functions
    std::vector<funcData> functions;
    //traverse ordered components
    const double accuracyThresh = 0.75;
    for(const EdgeGraph& graph: graphs){
        //iterate through paths
        for (const EdgePath& path : graph.paths){
            if(path.nodeIDs.size() < 2){
                continue;
            }
            //start point found in findPaths
            int head = 0;
            //sliding endpoint
            int tempTail = 1;
            //first function slope
            cv::Point p1 = graph.nodes[path.nodeIDs[head]].pixel;
            //cv::Point p2 = graph.nodes[path.nodeIDs[tail]].pixel;
            //cv fitline, param1 = default, param2 = distance tolerance, param3 = angle tolerance
            std::vector<cv::Point> linePixels;
            linePixels.push_back(p1);

            //keep track of the last function and pixels in case this pixel does not work
            double lastA = 0.0;
            double lastB = 0.0;
            double lastC = 0.0;
            double lastAccuracy = 0.0;
            bool hasValidFit = false;

            while(tempTail<static_cast<int>(path.nodeIDs.size())){
                cv::Point current = graph.nodes[path.nodeIDs[tempTail]].pixel;
                linePixels.push_back(current);
                //keeping the function updated with the cv function
                if(linePixels.size()>=2){
                    //cv function representation
                    cv::Vec4f fittedLine;
                    cv::fitLine(linePixels, fittedLine, cv::DIST_L2, 0, 0.01,0.01);
                    //convert into function struct format
                    double vx = fittedLine[0];
                    double vy = fittedLine[1];
                    double x0 = fittedLine[2];
                    double y0 = fittedLine[3];
                    //Ax + Bx + c = 0
                    double A = -vy;
                    double B = vx;
                    double C = vy * x0 - vx * y0;

                    int matchedPixels = 0;
                    double tolerance = 1.0;
                    //check the new accuracy with the additional pixels
                    for(const cv::Point& pixel : linePixels){
                        double distance = std::abs(A * pixel.x + B * pixel.y + C);
                        //calculate tolerance with range of image eventually
                        if(distance <= tolerance){
                            matchedPixels++;
                        }
                    }
                    double accuracy = static_cast<double>(matchedPixels) / linePixels.size();
                    if(accuracy >= accuracyThresh){
                        //allow new edge pixel
                        lastA = A;
                        lastB = B;
                        lastC = C;
                        lastAccuracy = accuracy;
                        hasValidFit = true;
                    }
                    else{
                        //pixel doesnt belong to this function
                        linePixels.pop_back();
                        if(hasValidFit){
                            A = lastA;
                            B = lastB;
                            C = lastC;
                            accuracy = lastAccuracy;                            
                        }
                    }
                }
                tempTail++;
            }

        }
    }

    return functions;
}
