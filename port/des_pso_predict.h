/*
 * des_pso_predict.h: the pipelines Demon's Souls can ask for, predicted from
 * the game's own files (des_pso_predict.cpp, docs/97), as pipeline index
 * rows (port/data/pso_index, docs/97).
 */
#pragma once

#include <cstdint>
#include <string>

/* The rows for the game in usrdir (its PS3_GAME/USRDIR) and its executable
 * (elf; empty: no engine programs), one a line, the most likely first. hdr:
 * the scene's colour target is HDR's (RSX_HDR), so its rows are the ones
 * made, not SDR's. shaders_of, when not null: only one row per shader that
 * none of its rows (pipeline index text) uses, the first that has it. A
 * driver compiles a shader the first time a pipeline in the process has it
 * (30-70 ms), and another pipeline with shaders it has seen takes about
 * 0.5 ms, so these rows are the ones to build and load at a start. counts,
 * when not null, get the rows written, the rows predicted, the materials
 * read, the programs, and the programs named by a rule that the files lack.
 * Empty when the files are not there. */
struct DesPsoPredictCounts {
    unsigned rows, predicted, materials, programs, missing;
};
std::string des_pso_predict(const std::string& usrdir, const std::string& elf, bool hdr,
                            DesPsoPredictCounts* counts, const std::string* shaders_of = nullptr);
