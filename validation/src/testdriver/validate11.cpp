/**
 * This software was developed at the National Institute of Standards and
 * Technology (NIST) by employees of the Federal Government in the course
 * of their official duties. Pursuant to title 17 Section 105 of the
 * United States Code, this software is not subject to copyright protection
 * and is in the public domain. NIST assumes no responsibility whatsoever for
 * its use by other parties, and makes no guarantees, expressed or implied,
 * about its quality, reliability, or any other characteristic.
 */

#include <fstream>
#include <iostream>
#include <cstring>
#include <iterator>
#include <sys/wait.h>
#include <unistd.h>

#include "frpc.h"
#include "util.h"

using namespace std;
using namespace FRPC;

int
readTemplateFromFile(
        const string &filename,
        vector<uint8_t> &templ)
{
    streampos fileSize;
    ifstream file(filename, ios::binary);

    if (!file.is_open()) {
        cerr << "Failed to open stream for " << filename << "." << endl;
        return FAILURE;
    }
    file.seekg(0, ios::end);
    fileSize = file.tellg();
    file.seekg(0, ios::beg);

    templ.resize(fileSize);
    file.read((char*)&templ[0], fileSize);
    return SUCCESS;
}

int
createTemplate(
        shared_ptr<VerifInterface> &implPtr,
        const string &inputFile,
        const string &outputLog,
        const string &templatesDir,
        TemplateRole role)
{
    /* Read input file */
    ifstream inputStream(inputFile);
    if (!inputStream.is_open()) {
        cerr << "Failed to open stream for " << inputFile << "." << endl;
        return FAILURE;
    }

    /* Open output log for writing */
    ofstream logStream(outputLog);
    if (!logStream.is_open()) {
        cerr << "Failed to open stream for " << outputLog << "." << endl;
        return FAILURE;
    }

    /* header */
    logStream << "id image templateSizeBytes returnCode isLeftEyeAssigned "
            "isRightEyeAssigned xleft yleft xright yright" << endl;

    string id, imagePath, desc;
    while (inputStream >> id >> imagePath) {
        Image face;
        if (!readImage(imagePath, face)) {
            cerr << "Failed to load image file: " << imagePath << "." << endl;
            return FAILURE;
        }

        vector<uint8_t> templ;
        EyePair eyes;
        auto ret = implPtr->createTemplate(face, role, templ, eyes);

        /* Open template file for writing */
        string templFile{id + ".template"};
        ofstream templStream(templatesDir + "/" + templFile);
        if (!templStream.is_open()) {
            cerr << "Failed to open stream for " << templatesDir + "/" + templFile << "." << endl;
            return FAILURE;
        }

        /* Write template file */
        templStream.write((char*)templ.data(), templ.size());

        /* Write template stats to log */
        logStream << id << " "
                << imagePath << " "
                << templ.size() << " "
                << static_cast<underlying_type<ReturnCode>::type>(ret.code) << " "
                << eyes.isLeftAssigned << " "
                << eyes.isRightAssigned << " "
                << eyes.xleft << " "
                << eyes.yleft << " "
                << eyes.xright << " "
                << eyes.yright << " "
                << endl;
    }
    inputStream.close();

    /* Remove the input file */
    if( remove(inputFile.c_str()) != 0 )
        cerr << "Error deleting file: " << inputFile << endl;

    return SUCCESS;
}

int
match(
        shared_ptr<VerifInterface> &implPtr,
        const string &inputFile,
        const string &templatesDir,
        const string &scoresLog)
{
    /* Read probes */
    ifstream inputStream(inputFile);
    if (!inputStream.is_open()) {
        cerr << "Failed to open stream for " << inputFile << "." << endl;
        return FAILURE;
    }

    /* Open scores log for writing */
    ofstream scoresStream(scoresLog);
    if (!scoresStream.is_open()) {
        cerr << "Failed to open stream for " << scoresLog << "." << endl;
        return FAILURE;
    }
    /* header */
    scoresStream << "enrollTempl verifTempl simScore returnCode" << endl;

    /* Process each probe */
    string enrollID, verifID;
    while (inputStream >> enrollID >> verifID) {
        vector<uint8_t> enrollTempl, verifTempl;
        double similarity = -1.0;
        /* Read templates from file */
        if (readTemplateFromFile(templatesDir + "/" + enrollID, enrollTempl) != SUCCESS) {
            cerr << "Unable to retrieve template from file : "
                    << templatesDir + "/" + enrollID << endl;
            return FAILURE;
        }
        if (readTemplateFromFile(templatesDir + "/" + verifID, verifTempl) != SUCCESS) {
            cerr << "Unable to retrieve template from file : "
                    << templatesDir + "/" + verifID << endl;
            return FAILURE;
        }

        /* Call match */
        auto ret = implPtr->matchTemplates(verifTempl, enrollTempl, similarity);

        /* Write to scores log file */
        scoresStream << enrollID << " "
                << verifID << " "
                << similarity << " "
                << static_cast<underlying_type<ReturnCode>::type>(ret.code)
                << endl;
    }
    inputStream.close();

    /* Remove the input file */
    if( remove(inputFile.c_str()) != 0 )
        cerr << "Error deleting file: " << inputFile << endl;

    return SUCCESS;
}

void usage(const string &executable)
{
    cerr << "Usage: " << executable << " enroll|verif|match -c configDir "
            "-o outputDir -h outputStem -i inputFile -t numForks -j templatesDir" << endl;
    exit(EXIT_FAILURE);
}

int
main(
        int argc,
        char* argv[])
{
    auto exitStatus = SUCCESS;
    int requiredArgs = 2; /* exec name and action */
    if (argc < requiredArgs)
        usage(argv[0]);

    string actionstr{argv[1]},
        configDir{"config"},
        outputDir{"output"},
        outputFileStem{"stem"},
        inputFile,
        templatesDir;
    int numForks = 1;

    for (int i = 0; i < argc - requiredArgs; i++) {
        if (strcmp(argv[requiredArgs+i],"-c") == 0)
            configDir = argv[requiredArgs+(++i)];
        else if (strcmp(argv[requiredArgs+i],"-o") == 0)
            outputDir = argv[requiredArgs+(++i)];
        else if (strcmp(argv[requiredArgs+i],"-h") == 0)
            outputFileStem = argv[requiredArgs+(++i)];
        else if (strcmp(argv[requiredArgs+i],"-i") == 0)
            inputFile = argv[requiredArgs+(++i)];
        else if (strcmp(argv[requiredArgs+i],"-j") == 0)
            templatesDir = argv[requiredArgs+(++i)];
        else if (strcmp(argv[requiredArgs+i],"-t") == 0)
            numForks = atoi(argv[requiredArgs+(++i)]);
        else {
            cerr << "Unrecognized flag: " << argv[requiredArgs+i] << endl;;
            usage(argv[0]);
        }
    }

    Action action;
    TemplateRole role = TemplateRole::Enrollment_11;
    if (actionstr == "enroll" || actionstr == "verif") {
        action = Action::CreateTemplate_11;
        if (actionstr == "enroll") role = TemplateRole::Enrollment_11;
        else role = TemplateRole::Verification_11;
    } else if(actionstr == "match")
        action = Action::Match_11;
    else {
        cerr << "Unknown command: " << actionstr << endl;
        usage(argv[0]);
    }

    /* Get implementation pointer */
    auto implPtr = VerifInterface::getImplementation();
    /* Initialization */
    auto ret = implPtr->initialize(configDir);
    if (ret.code != ReturnCode::Success) {
        cerr << "initialize() returned error code: "
                << ret.code << "." << endl;
        return FAILURE;
    }

    /* Split input file into appropriate number of splits */
    vector<string> inputFileVector;
    if (splitInputFile(inputFile, outputDir, numForks, inputFileVector) != SUCCESS) {
        cerr << "An error occurred with processing the input file." << endl;
        return FAILURE;
    }

    bool parent = false;
    if (numForks == 1) {
        if (action == Action::CreateTemplate_11)
            return createTemplate(
                    implPtr,
                    inputFileVector[0],
                    outputDir + "/" + outputFileStem + ".log.0",
                    templatesDir,
                    role);
        else if (action == Action::Match_11)
            return match(
                    implPtr,
                    inputFileVector[0],
                    templatesDir,
                    outputDir + "/" + outputFileStem + ".log.0");
    } else if (numForks > 1) {
        int i = 0;
        for (auto &inputFile : inputFileVector) {
            /* Fork */
            switch(fork()) {
            case 0: /* Child */
                if (action == Action::CreateTemplate_11)
                    return createTemplate(
                            implPtr,
                            inputFile,
                            outputDir + "/" + outputFileStem + ".log." + to_string(i),
                            templatesDir,
                            role);
                else if (action == Action::Match_11)
                    return match(
                            implPtr,
                            inputFile,
                            templatesDir,
                            outputDir + "/" + outputFileStem + ".log." + to_string(i));
            case -1: /* Error */
                cerr << "Problem forking" << endl;
                break;
            default: /* Parent */
                parent = true;
                break;
            }
            i++;
        }
    }

    /* Parent -- wait for children */
    if (parent) {
        while (numForks > 0) {
            int stat_val;
            pid_t cpid;

            cpid = wait(&stat_val);
            if (WIFEXITED(stat_val)) {}
            else if (WIFSIGNALED(stat_val)) {
                cerr << "PID " << cpid << " exited due to signal " <<
                        WTERMSIG(stat_val) << endl;
                exitStatus = FAILURE;
            } else {
                cerr << "PID " << cpid << " exited with unknown status." << endl;
                exitStatus = FAILURE;
            }
            numForks--;
        }
    }

    return exitStatus;
}
