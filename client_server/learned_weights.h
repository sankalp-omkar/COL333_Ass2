// Auto-generated learned weights
// Generated from machine learning on game logs using PyTorch
// Both-sides training (learned from circle AND square perspectives)
// Include this file in your C++ agent

#ifndef LEARNED_WEIGHTS_H
#define LEARNED_WEIGHTS_H

namespace LearnedWeights {
    // Feature weights learned from training data
    
    // Scoring Features
    constexpr double MY_SCORING_STONES = 0.0887904763;
    constexpr double OPP_SCORING_STONES = 0.0999981612;
    
    // Distance Features
    constexpr double MY_MIN_DISTANCE = 0.0868278444;
    constexpr double MY_PROXIMITY_SCORE = 0.0763051361;
    constexpr double MY_STONES_WITHIN_2 = 0.1089616120;
    constexpr double MY_STONES_WITHIN_4 = 0.0923513696;
    constexpr double MY_STONES_WITHIN_6 = 0.1032099500;
    constexpr double MY_STONES_WITHIN_1_ROWS = 0.1019953862;
    constexpr double OPP_MIN_DISTANCE = 0.1020607948;
    constexpr double OPP_PROXIMITY_SCORE = 0.0606093593;
    constexpr double OPP_STONES_WITHIN_2 = 0.0787663907;
    constexpr double OPP_STONES_WITHIN_4 = 0.0813723952;
    constexpr double OPP_STONES_WITHIN_6 = 0.1063645780;

    // River Features
    constexpr double MY_RIVER_COUNT = 0.0576938763;
    constexpr double MY_RIVERS_HORIZONTAL = 0.0836545005;
    constexpr double MY_RIVERS_NEAR_GOAL = 0.0812201947;
    constexpr double RIVERS_NEAR_US = 0.0850072652;
    constexpr double MY_RIVERS_VERTICAL = 0.0791918263;
    constexpr double OPP_RIVER_COUNT = 0.0769866705;
    constexpr double OPP_RIVERS_NEAR_GOAL = 0.0683374256;

    // Blocking Features
    constexpr double MY_BLOCKING_PIECES = 0.0379655324;
    constexpr double OPP_STONES_BLOCKED = 0.0788687617;

    // Tempo Features
    constexpr double ADVANCEMENT_DIFF = 0.0479859263;
    constexpr double MY_ADVANCEMENT = 0.0628373772;
    constexpr double OPP_ADVANCEMENT = 0.0429718792;

    // Other Features
    constexpr double MY_CLEAR_PATHS_TO_GOAL = 0.0739082694;

}

#endif // LEARNED_WEIGHTS_H