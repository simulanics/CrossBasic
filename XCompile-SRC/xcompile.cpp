/*

  xcompile.cpp
  CrossBasic App Compiler: xcompile                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
  Copyright (c) 2025 Simulanics Technologies – Matthew Combatti
  All rights reserved.
 
  Licensed under the CrossBasic Source License (CBSL-1.1).
  You may not use this file except in compliance with the License.
  You may obtain a copy of the License at:
  https://www.crossbasic.com/license
 
  SPDX-License-Identifier: CBSL-1.1
  
  Author:
    The AI Team under direction of Matthew Combatti <mcombatti@crossbasic.com>
    
*/ 

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>

const char MARKER[9] = "BYTECODE"; // 8 characters + null terminator
std::string cipherkey = "MySecretKey12345";

// XTEA encryption: operates on 64-bit blocks (two 32-bit values)
void xtea_encrypt(uint32_t v[2], const uint32_t key[4]) {
    uint32_t v0 = v[0], v1 = v[1], sum = 0;
    const uint32_t delta = 0x9E3779B9;
    for (unsigned int i = 0; i < 32; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

void xtea_decrypt(uint32_t v[2], const uint32_t key[4]) {
    uint32_t v0 = v[0], v1 = v[1];
    const uint32_t delta = 0x9E3779B9;
    uint32_t sum = delta * 32;
    for (unsigned int i = 0; i < 32; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
        sum -= delta;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

// Encrypts a plaintext string with a key and returns raw binary ciphertext.
std::string encrypt(const std::string &plaintext, const std::string &keyStr) {
    // Prepend the original length (4 bytes, little-endian) for unpadding.
    uint32_t origLen = static_cast<uint32_t>(plaintext.size());
    std::string data;
    for (int i = 0; i < 4; i++) {
        data.push_back(static_cast<char>((origLen >> (i * 8)) & 0xFF));
    }
    data.append(plaintext);
    // Pad data to a multiple of 8 bytes.
    size_t pad = 8 - (data.size() % 8);
    if (pad != 8) {
        data.append(pad, '\0');
    }

    // Derive a 128-bit key from keyStr (first 16 bytes, padded with zeros if needed).
    uint32_t key[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (i < keyStr.size())
            key[i / 4] |= ((uint32_t)(unsigned char)keyStr[i]) << ((i % 4) * 8);
    }

    std::string cipher = data;
    // Process each 8-byte block.
    for (size_t i = 0; i < data.size(); i += 8) {
        uint32_t block[2] = {0, 0};
        for (int j = 0; j < 4; j++) {
            block[0] |= ((uint32_t)(unsigned char)data[i + j]) << (j * 8);
            block[1] |= ((uint32_t)(unsigned char)data[i + 4 + j]) << (j * 8);
        }
        xtea_encrypt(block, key);
        for (int j = 0; j < 4; j++) {
            cipher[i + j] = static_cast<char>((block[0] >> (j * 8)) & 0xFF);
            cipher[i + 4 + j] = static_cast<char>((block[1] >> (j * 8)) & 0xFF);
        }
    }
    // Return raw binary ciphertext.
    return cipher;
}

// Decrypts the binary ciphertext with the given key and returns the original plaintext.
std::string decrypt(const std::string &cipher, const std::string &keyStr) {
    if (cipher.size() % 8 != 0) return "";
    uint32_t key[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (i < keyStr.size())
            key[i / 4] |= ((uint32_t)(unsigned char)keyStr[i]) << ((i % 4) * 8);
    }
    std::string plain = cipher;
    for (size_t i = 0; i < cipher.size(); i += 8) {
        uint32_t block[2] = {0, 0};
        for (int j = 0; j < 4; j++) {
            block[0] |= ((uint32_t)(unsigned char)cipher[i + j]) << (j * 8);
            block[1] |= ((uint32_t)(unsigned char)cipher[i + 4 + j]) << (j * 8);
        }
        xtea_decrypt(block, key);
        for (int j = 0; j < 4; j++) {
            plain[i + j] = static_cast<char>((block[0] >> (j * 8)) & 0xFF);
            plain[i + 4 + j] = static_cast<char>((block[1] >> (j * 8)) & 0xFF);
        }
    }
    if (plain.size() < 4) return "";
    uint32_t origLen = 0;
    for (int i = 0; i < 4; i++) {
        origLen |= ((uint32_t)(unsigned char)plain[i]) << (i * 8);
    }
    return plain.substr(4, origLen);
}

// Copies a file from sourcePath to destPath.
bool copyFile(const std::string& sourcePath, const std::string& destPath) {
    std::ifstream src(sourcePath, std::ios::binary);
    if (!src) {
        std::cerr << "Error: Unable to open source file " << sourcePath << "\n";
        return false;
    }
    std::ofstream dst(destPath, std::ios::binary);
    if (!dst) {
        std::cerr << "Error: Unable to create destination file " << destPath << "\n";
        return false;
    }
    dst << src.rdbuf();
    return true;
}

// Checks if the executable already has embedded data.
bool hasEmbeddedData(const std::string& exePath) {
    std::ifstream file(exePath, std::ios::binary);
    if (!file)
        return false;
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    if (fileSize < 12)
        return false;
    file.seekg(-12, std::ios::end);
    char markerBuffer[9] = {0};
    file.read(markerBuffer, 8);
    return (std::strncmp(markerBuffer, MARKER, 8) == 0);
}

// Injects encrypted text file contents into the executable by appending:
// [encrypted data][MARKER (8 bytes)][4-byte encrypted data length]
void injectData(const std::string& exePath, const std::string& textFilePath) {
    // Read text file into a vector.
    std::ifstream textFile(textFilePath, std::ios::binary);
    if (!textFile) {
        std::cerr << "Error: Cannot open source file " << textFilePath << ".\n";
        return;
    }
    std::vector<char> textData((std::istreambuf_iterator<char>(textFile)),
                               std::istreambuf_iterator<char>());
    textFile.close();

    // Convert to string and encrypt the contents.
    std::string plaintext(textData.begin(), textData.end());
    std::string encryptedData = encrypt(plaintext, cipherkey);
    uint32_t dataLength = static_cast<uint32_t>(encryptedData.size());

    // Open the executable for reading and writing in binary mode.
    std::fstream exeFile(exePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!exeFile) {
        std::cerr << "Error: Cannot open executable path " << exePath << " for writing.\n";
        return;
    }

    // Append the encrypted data, marker (8 bytes), and encrypted data length (4 bytes) at the end.
    exeFile.seekp(0, std::ios::end);
    exeFile.write(encryptedData.data(), dataLength);
    exeFile.write(MARKER, 8);
    exeFile.write(reinterpret_cast<const char*>(&dataLength), sizeof(dataLength));

    exeFile.close();
    std::cout << "Compilation complete: Wrote " << dataLength
              << " bytes of encrypted bytecode to " << exePath << ".\n";
}

int main(int argc, char* argv[]) {
    // Require 2 mandatory arguments, plus optional "-GUI"
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <target_executable> <text_file> [-GUI]\n";
        return EXIT_FAILURE;
    }

    std::string targetExe   = argv[1];
    std::string textFilePath = argv[2];
    bool useGUI = false;

    // Handle optional GUI flag
    if (argc == 4) {
        std::string flag = argv[3];
        if (flag == "-GUI") {
            useGUI = true;
        }
        else {
            std::cerr << "Error: Unknown parameter '" << flag << "'.\n"
                      << "Usage: " << argv[0]
                      << " <target_executable> <text_file> [-GUI]\n";
            return EXIT_FAILURE;
        }
    }

    // Prevent the use of the base executable name directly.
    if (targetExe == "crossbasic"   ||
        targetExe == "crossbasic.exe"||
        targetExe == "crossbasicw"   ||
        targetExe == "crossbasicw.exe")
    {
        std::cerr << "Error: Cannot use base executable name as target.\n";
        return EXIT_FAILURE;
    }

    // Determine the directory of the current executable.
    std::string currentPath = argv[0];
    size_t pos = currentPath.find_last_of("\\/");
    std::string baseDir = (pos != std::string::npos)
                          ? currentPath.substr(0, pos + 1)
                          : "";

#ifdef _WIN32
    const char* baseName = useGUI
        ? "crossbasicw.exe"
        : "crossbasic.exe";
#else
    const char* baseName = useGUI
        ? "crossbasicw"
        : "crossbasic";
#endif

    // Build full path to the base executable:
    std::string baseExe = baseDir + baseName;

    // Copy the base executable to the user-defined target filename.
    if (!copyFile(baseExe, targetExe)) {
        std::cerr << "Error: Could not copy base executable from "
                  << baseExe << " to " << targetExe << ".\n";
        return EXIT_FAILURE;
    }

    // Check if the target executable already has embedded data.
    if (hasEmbeddedData(targetExe)) {
        std::cerr << "Error: The executable already contains embedded code and cannot be overwritten.\n";
        return EXIT_FAILURE;
    }

    // Inject the encrypted text file contents into the target executable.
    injectData(targetExe, textFilePath);

    return EXIT_SUCCESS;
}