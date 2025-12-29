#include "libbbf.h"
#include "xxhash.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>

// I HATE WINDOWS (but alas, i'll work with it.)
// kept getting issues with doing utf-8 stuff in the terminal so I added this little thing.
#ifdef _WIN32
#include <windows.h>

// Convert UTF-16 (Windows default) to UTF-8
std::string UTF16toUTF8(const std::wstring &wstr)
{
    if (wstr.empty())
        return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
#endif

namespace fs = std::filesystem;

class BBFReader
{
public:
    BBFFooter footer;
    BBFHeader header; // Added to store header info
    std::ifstream stream;
    std::vector<char> stringPool;

    bool open(const std::string &path)
    {
        stream.open(path, std::ios::binary | std::ios::ate);
        if (!stream.is_open())
            return false;

        size_t fileSize = stream.tellg();

        // read header
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char *>(&header), sizeof(BBFHeader));

        // validate header
        if (std::string((char *)header.magic, 4) != "BBF1")
            return false;

        // read footer
        stream.seekg(fileSize - sizeof(BBFFooter));
        stream.read(reinterpret_cast<char *>(&footer), sizeof(BBFFooter));

        if (std::string((char *)footer.magic, 4) != "BBF1")
            return false;

        // Load string pool
        stringPool.resize(footer.assetTableOffset - footer.stringPoolOffset);
        stream.seekg(footer.stringPoolOffset);
        stream.read(stringPool.data(), stringPool.size());
        return true;
    }

    std::string getString(uint32_t offset)
    {
        if (offset >= stringPool.size())
            return "OFFSET_ERR";
        return std::string(stringPool.data() + offset);
    }

    std::vector<BBFAssetEntry> getAssets()
    {
        std::vector<BBFAssetEntry> assets(footer.assetCount);
        stream.seekg(footer.assetTableOffset);
        stream.read(reinterpret_cast<char *>(assets.data()), footer.assetCount * sizeof(BBFAssetEntry));
        return assets;
    }

    std::vector<BBFPageEntry> getPages()
    {
        std::vector<BBFPageEntry> pages(footer.pageCount);
        stream.seekg(footer.pageTableOffset);
        stream.read(reinterpret_cast<char *>(pages.data()), footer.pageCount * sizeof(BBFPageEntry));
        return pages;
    }

    std::vector<BBFSection> getSections()
    {
        std::vector<BBFSection> sections(footer.sectionCount);
        stream.seekg(footer.sectionTableOffset);
        stream.read(reinterpret_cast<char *>(sections.data()), footer.sectionCount * sizeof(BBFSection));
        return sections;
    }

    std::vector<BBFMetadata> getMetadata()
    {
        std::vector<BBFMetadata> meta(footer.keyCount);
        if (footer.keyCount > 0)
        {
            stream.seekg(footer.metaTableOffset);
            stream.read(reinterpret_cast<char *>(meta.data()), footer.keyCount * sizeof(BBFMetadata));
        }
        return meta;
    }
};

void printHelp()
{
    std::cout << "Bound Book Format Muxer (bbfmux) - Archival Sequential Image Container\n"
                 "-----------------------------------------------------------------------\n"
                 "Usage:\n"
                 "  Muxing:     bbfmux <inputs...> [options] <output.bbf>\n"
                 "  Info:       bbfmux <file.bbf> --info\n"
                 "  Verify:     bbfmux <file.bbf> --verify\n"
                 "  Extract:    bbfmux <file.bbf> --extract [--outdir=path] [--section=\"Name\"]\n"
                 "\n"
                 "Inputs:\n"
                 "  Can be individual image files (.png or .avif) or directories.\n"
                 "  Files are sorted alphabetically. Data is 4KB sector-aligned for performance.\n"
                 "\n"
                 "Options (Muxing):\n"
                 "  --section=Name:Page[:Parent]  Add a section marker (1-based page index).\n"
                 "                                Optional: Provide a Parent name to nest chapters.\n"
                 "  --meta=Key:Value              Add archival metadata (Title, Author, etc.).\n"
                 "\n"
                 "Options (Extraction):\n"
                 "  --outdir=path                 Output directory (default: ./extracted).\n"
                 "  --section=Name                Extract only a specific section/volume.\n"
                 "\n"
                 "Global Options:\n"
                 "  --info                        Display book structure and metadata.\n"
                 "  --verify                      Perform XXH3 integrity check on all assets.\n"
                 "\n"
                 "Examples:\n"
                 "  [Creation with Hierarchy]\n"
                 "    bbfmux ./vol1/ --section=\"Volume 1\":1 --section=\"Chapter 1\":1:\"Volume 1\" out.bbf\n"
                 "\n"
                 "  [Adding Metadata]\n"
                 "    bbfmux out.bbf --meta=Title:\"Akira\" --meta=Author:\"Otomo\"\n"
                 "\n"
                 "  [Extracting a Volume]\n"
                 "    bbfmux comic.bbf --extract --section=\"Volume 1\" --outdir=\"./V1\"\n"
                 "\n"
                 "  [Checking Integrity]\n"
                 "    bbfmux comic.bbf --verify\n"
              << std::endl;
}

std::string trimQuotes(const std::string &s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

#ifdef _WIN32
int wmain(int argc, wchar_t *argv[])
{
    // Set console output to UTF-8 so std::cout works with Korean/Japanese/etc.
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IOFBF, 1000); // Buffer fix for some terminals

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(UTF16toUTF8(argv[i]));
    }
#else
int main(int argc, char *argv[])
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }
#endif
    // Now use 'args' instead of 'argv' throughout your code
    if (args.size() < 2)
    {
        printHelp();
        return 1;
    }

    std::vector<std::string> inputs;
    std::string outputBbf;
    bool modeInfo = false, modeVerify = false, modeExtract = false;
    std::string outDir = "./extracted";
    std::string targetSection = "";

    struct SecReq
    {
        std::string name;
        std::string parent;
        uint32_t page;
    };
    struct MetaReq
    {
        std::string k, v;
    };
    std::vector<SecReq> secReqs;
    std::vector<MetaReq> metaReqs;

    // Parse all of the arguments
    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string arg = args[i]; // Use args[i], not argv[i]

        if (arg == "--info")
            modeInfo = true;
        else if (arg == "--verify")
            modeVerify = true;
        else if (arg == "--extract")
            modeExtract = true;
        else if (arg.find("--outdir=") == 0)
            outDir = arg.substr(9);
        else if (arg.find("--section=") == 0)
        {
            std::string val = arg.substr(10);
            // Split by ':' - supports Name:Page or Name:Page:Parent
            std::vector<std::string> parts;
            size_t start = 0, end = 0;
            while ((end = val.find(':', start)) != std::string::npos)
            {
                parts.push_back(val.substr(start, end - start));
                start = end + 1;
            }
            parts.push_back(val.substr(start));

            if (modeExtract)
            {
                targetSection = trimQuotes(parts[0]);
            }
            else if (parts.size() >= 2)
            {
                SecReq req;
                req.name = trimQuotes(parts[0]);
                req.page = (uint32_t)std::stoi(parts[1]);
                if (parts.size() >= 3)
                    req.parent = trimQuotes(parts[2]); // Added parent field to SecReq struct
                secReqs.push_back(req);
            }
        }
        else if (arg.find("--meta=") == 0)
        {
            std::string val = arg.substr(7);
            size_t colon = val.find(':');
            if (colon != std::string::npos)
                metaReqs.push_back({val.substr(0, colon), val.substr(colon + 1)});
        }
        else
        {
            inputs.push_back(arg);
        }
    }
    // Perform actions
    if (modeInfo || modeVerify || modeExtract)
    {
        if (inputs.empty())
        {
            std::cerr << "Error: No .bbf input specified.\n";
            return 1;
        }
        BBFReader reader;
        if (!reader.open(inputs[0]))
        {
            std::cerr << "Error: Failed to open BBF.\n";
            return 1;
        }

        if (modeInfo)
        {
            std::cout << "Bound Book Format (.bbf) Info\n";
            std::cout << "------------------------------\n";
            std::cout << "BBF Version: " << (int)reader.header.version << "\n";
            std::cout << "Pages:       " << reader.footer.pageCount << "\n";
            std::cout << "Assets:      " << reader.footer.assetCount << " (Deduplicated)\n";

            // Print Sections
            std::cout << "\n[Sections]\n";
            auto sections = reader.getSections();
            if (sections.empty())
            {
                std::cout << " No sections defined.\n";
            }
            else
            {
                for (auto &s : sections)
                {
                    std::cout << " - " << std::left << std::setw(20)
                              << reader.getString(s.sectionTitleOffset)
                              << " (Starting Page: " << s.sectionStartIndex + 1 << ")\n";
                }
            }

            // Print Metadata
            std::cout << "\n[Metadata]\n";
            auto metadata = reader.getMetadata();
            if (metadata.empty())
            {
                std::cout << " No metadata found.\n";
            }
            else
            {
                for (auto &m : metadata)
                {
                    std::string key = reader.getString(m.keyOffset);
                    std::string val = reader.getString(m.valOffset);
                    std::cout << " - " << std::left << std::setw(15) << (key + ":") << val << "\n";
                }
            }
            std::cout << std::endl;
        }

        if (modeVerify)
        {
            std::cout << "Verifying asset integrity...\n";
            auto assets = reader.getAssets();
            bool clean = true;
            for (size_t i = 0; i < assets.size(); ++i)
            {
                std::vector<char> buf(assets[i].length);
                reader.stream.seekg(assets[i].offset);
                reader.stream.read(buf.data(), assets[i].length);
                if (XXH3_64bits(buf.data(), buf.size()) != assets[i].xxh3Hash)
                {
                    std::cerr << "Mismatch in asset " << i << "\n";
                    clean = false;
                }
            }
            if (clean)
                std::cout << "Integrity Check Passed.\n";
        }

        if (modeExtract)
        {
            fs::create_directories(outDir);
            auto pages = reader.getPages();
            auto assets = reader.getAssets();
            auto sections = reader.getSections();

            uint32_t start = 0, end = (uint32_t)pages.size();
            if (!targetSection.empty())
            {
                bool found = false;
                for (size_t i = 0; i < sections.size(); ++i)
                {
                    if (trimQuotes(reader.getString(sections[i].sectionTitleOffset)) == targetSection)
                    {
                        start = sections[i].sectionStartIndex;
                        uint32_t myParent = sections[i].parentSectionIndex;

                        // Find the 'End' page:
                        // Search for the next section that is NOT a child of this one
                        // and starts at a later page.
                        end = (uint32_t)pages.size();
                        for (size_t j = i + 1; j < sections.size(); ++j)
                        {
                            if (sections[j].sectionStartIndex > start && sections[j].parentSectionIndex == myParent)
                            {
                                end = sections[j].sectionStartIndex;
                                break;
                            }
                        }
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    std::cerr << "Section '" << targetSection << "' not found.\n";
                    return 1;
                }
            }

            std::cout << "Extracting " << targetSection << " (Pages " << (start + 1) << " to " << end << ") to " << outDir << "...\n";

            for (uint32_t i = start; i < end; ++i)
            {
                auto &asset = assets[pages[i].assetIndex];
                std::string ext = (asset.type == 0x01) ? ".avif" : ".png";
                std::string outPath = (fs::path(outDir) / ("page_" + std::to_string(i + 1) + ext)).string();

                std::vector<char> buf(asset.length);
                reader.stream.seekg(asset.offset);
                reader.stream.read(buf.data(), asset.length);

                std::ofstream ofs(outPath, std::ios::binary);
                ofs.write(buf.data(), asset.length);
            }
            std::cout << "Extracted " << (end - start) << " pages.\n";
        }
    }
    else
    {
        if (inputs.size() < 2)
        {
            std::cerr << "Error: Provide inputs and an output filename.\n";
            return 1;
        }
        outputBbf = inputs.back();
        inputs.pop_back();

        BBFBuilder builder(outputBbf);

        std::vector<std::string> imagePaths;
        for (const auto &path : inputs)
        {
            if (fs::is_directory(path))
            {
                for (const auto &entry : fs::directory_iterator(path))
                    imagePaths.push_back(entry.path().string());
            }
            else
                imagePaths.push_back(path);
        }
        std::sort(imagePaths.begin(), imagePaths.end());

        for (const auto &p : imagePaths)
        {
            std::string ext = fs::path(p).extension().string();
            uint8_t type = (ext == ".avif" || ext == ".AVIF") ? 1 : 2;
            builder.addPage(p, type);
        }

        // Parent Resolution Map
        std::unordered_map<std::string, uint32_t> sectionNameToIdx;
        uint32_t sectionCounter = 0;

        for (auto &s : secReqs)
        {
            uint32_t parentIdx = 0xFFFFFFFF; // Default: No parent
            if (!s.parent.empty() && sectionNameToIdx.count(s.parent))
            {
                parentIdx = sectionNameToIdx[s.parent];
            }

            builder.addSection(s.name, s.page - 1, parentIdx);
            sectionNameToIdx[s.name] = sectionCounter++;
        }

        for (auto &m : metaReqs)
        {
            // Use trimQuotes to ensure metadata keys/values don't have stray " characters
            builder.addMetadata(trimQuotes(m.k), trimQuotes(m.v));
        }

        if (builder.finalize())
            std::cout << "Successfully created " << outputBbf << "\n";

    }

    return 0;
}