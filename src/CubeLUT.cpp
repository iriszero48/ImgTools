#include "CubeLUT.h"

#include <sstream>

namespace Lut
{
    std::string CubeLut::ReadLine(std::ifstream& infile, char lineSeparator)
    {
        // Skip empty lines and comments 
        const char CommentMarker = '#';
        std::string textLine("");
        while (textLine.size() == 0 || textLine[0] == CommentMarker)
        {
            if (infile.eof()) { status = LutState::PrematureEndOfFile; break; }
            getline(infile, textLine, lineSeparator);
            if (infile.fail()) { status = LutState::ReadError; break; }
        }
        return textLine;
    } // ReadLine 

    CubeLut::Row CubeLut::ParseTableRow(const std::string& lineOfText)
    {
        constexpr int N = 3;
        float f[N];
        std::istringstream line(lineOfText);
        for (float& i : f)
        {
            line >> i;
            if (line.fail())
            {
	            status = LutState::CouldNotParseTableData;
            	break;
            }
        }
        return Row(f);
    } // ParseTableRow 

    const CubeLut::TableType& CubeLut::GetTable() const
    {
        return table;
    }

	CubeLut::Dim CubeLut::GetDim() const
    {
        return std::visit([&]<typename T0>(const T0& tb)
        {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, Table1D>)
            {
                return Dim::_1D;
            }
        	if constexpr (std::is_same_v<T, Table3D>)
            {
                return Dim::_3D;
            }
            else
            {
                throw std::runtime_error("non-exhaustive visitor!");
            }
        }, table);
    }

    CubeLut::LutState   CubeLut::LoadCubeFile(std::ifstream& infile)
    {
        // Set defaults 
        status = LutState::OK;
        Title.clear();
        DomainMin = Row(0.0);
        DomainMax = Row(1.0);

        // Read file data line by line 
        const char NewlineCharacter = '\n';
        char lineSeparator = NewlineCharacter;

        // sniff use of legacy lineSeparator 
        const char CarriageReturnCharacter = '\r';
        for (int i = 0; i < 255; i++)
        {
            char inc = infile.get();
            if (inc == NewlineCharacter) break;
            if (inc == CarriageReturnCharacter)
            {
                if (infile.get() == NewlineCharacter) break;
                lineSeparator = CarriageReturnCharacter;
                //clog << "INFO: This file uses non-compliant line separator \\r (0x0D)" << endl;
                break;
            }
            if (i > 250)
            {
	            status = LutState::LineError;
            	break;
            }
        }
        infile.seekg(0);
        infile.clear();

        // read keywords 
        int N, CntTitle, CntSize, CntMin, CntMax;
        // each keyword to occur zero or one time 
        N = CntTitle = CntSize = CntMin = CntMax = 0;

        while (status == LutState::OK)
        {
            long linePos = infile.tellg();
            std::string lineOfText = ReadLine(infile, lineSeparator);
            if (status != LutState::OK) break;

            // Parse keywords and parameters 
            std::istringstream line(lineOfText);
            std::string keyword;
            line >> keyword;

            if ("+" < keyword && keyword < ":")
            {
                // lines of table data come after keywords 
                // restore stream pos to re-read line of data 
                infile.seekg(linePos);
                break;
            }

            if (keyword == "TITLE" && CntTitle++ == 0) {
	            const char QUOTE = '"';
	            char startOfTitle;
	            line >> startOfTitle;
	            if (startOfTitle != QUOTE) { status = LutState::TitleMissingQuote; break; }
	            getline(line, Title, QUOTE);  // read to " 
            }
            else if (keyword == "DOMAIN_MIN" && CntMin++ == 0)
            {
                float domainMin[3];
	            line >> domainMin[0] >> domainMin[1] >> domainMin[2];
                DomainMin = Row(domainMin);
            }
            else if (keyword == "DOMAIN_MAX" && CntMax++ == 0)
            {
                float domainMax[3];
	            line >> domainMax[0] >> domainMax[1] >> domainMax[2];
                DomainMax = Row(domainMax);
            }
            else if (keyword == "LUT_1D_SIZE" && CntSize++ == 0)
            {
	            line >> N;
	            if (N < 2 || N > 65536) { status = LutState::LUTSizeOutOfRange; break; }
                table = Table1D(N);
            }
            else if (keyword == "LUT_3D_SIZE" && CntSize++ == 0)
            {
	            line >> N;
	            if (N < 2 || N > 256) { status = LutState::LUTSizeOutOfRange; break; }
                table = Table3D(N);
            }
            else
            {
	            status = LutState::UnknownOrRepeatedKeyword;
            	break;
            }

            if (line.fail())
            {
	            status = LutState::ReadError;
            	break;
            }
        } // read keywords 

        if (status == LutState::OK && CntSize == 0) status = LutState::LUTSizeOutOfRange;

        if (status == LutState::OK && (DomainMin.B >= DomainMax.B || DomainMin.G >= DomainMax.G
            || DomainMin.R >= DomainMax.R))
            status = LutState::DomainBoundsReversed;

        // read lines of table data
        std::visit([&]<typename T0>(T0& tb)
            {
	            using T = std::decay_t<T0>;
	            if constexpr (std::is_same_v<T, Table1D>)
	            {
                    for (int i = 0; i < tb.Length() && status == LutState::OK; i++)
                    {
                        tb.At(i) = ParseTableRow(ReadLine(infile, lineSeparator));
                    }
	            }
	            else if constexpr (std::is_same_v<T, Table3D>)
	            {
                    for (int b = 0; b < N && status == LutState::OK; b++)
                    {
                        for (int g = 0; g < N && status == LutState::OK; g++)
                        {
                            for (int r = 0; r < N && status == LutState::OK; r++)
                            {
                                tb.At(r, g, b) = ParseTableRow(ReadLine(infile, lineSeparator));

                            }
                        }
                    }
	            }
	            else
	            {
                    throw std::runtime_error("non-exhaustive visitor!");
	            }
            }, table);
    	// read 3D LUT
        return status;
    } // LoadCubeFile 

    CubeLut::LutState   CubeLut::SaveCubeFile(std::ofstream& outfile)
    {
        if (status != LutState::OK) return status; // Write only good Cubes 

        // Write keywords 
        const char SPACE = ' ';
        const char QUOTE = '"';

        if (!Title.empty()) outfile << "TITLE" << SPACE << QUOTE << Title << QUOTE << std::endl;
        outfile << "# Created by CubeLUT.cpp" << std::endl;
        outfile << "DOMAIN_MIN" << SPACE << DomainMin.R << SPACE << DomainMin.G
            << SPACE << DomainMin.B << std::endl;
        outfile << "DOMAIN_MAX" << SPACE << DomainMax.R << SPACE << DomainMax.G
            << SPACE << DomainMax.B << std::endl;

        // Write LUT data
        std::visit([&]<typename T0>(const T0& tb)
        {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, Table1D>)
            {
                const auto N = tb.Length();
                outfile << "LUT_1D_SIZE" << SPACE << N << std::endl;
                for (int i = 0; i < N && outfile.good(); i++)
                {
                    const auto [r, g, b] = tb.At(i);
                    outfile << r << SPACE << g << SPACE << b << std::endl;
                }
            }
            else if constexpr (std::is_same_v<T, Table3D>)
            {
            	const auto N = tb.Length();
                outfile << "LUT_3D_SIZE" << SPACE << N << std::endl;
                // NOTE that r loops fastest 
                for (int b = 0; b < N && outfile.good(); b++)
                {
                    for (int g = 0; g < N && outfile.good(); g++)
                    {
                        for (int r = 0; r < N && outfile.good(); r++)
                        {
                            const auto [rv, gv, bv] = tb.At(r, g, b);
                            outfile << rv << SPACE << gv << SPACE << bv << std::endl;
                        }
                    }
                }
            }
            else
            {
                throw std::runtime_error("non-exhaustive visitor!");
            }
        }, table);
    	// write 3D LUT 

        outfile.flush();
        return (outfile.good() ? LutState::OK : LutState::WriteError);
    } // SaveCubeFile

    CubeLut CubeLut::FromCubeFile(const std::filesystem::path& file)
    {
        CubeLut cube;
        enum { OK = 0, ErrorOpenInFile = 100, ErrorOpenOutFile };

        // Load a Cube 
        std::ifstream infile(file);
        if (!infile)
        {
            throw std::runtime_error("Could not open input file");
            //std::cout << "Could not open input file " << cubeFileIn << std::endl;
            //return ErrorOpenInFile;
        }
        auto ret = cube.LoadCubeFile(infile);

        infile.close();
        if (ret != LutState::OK)
        {
            throw std::runtime_error("Could not parse the cube info in the input file.");
            //std::cout << "Could not parse the cube info in the input file. Return code = "
            //    << (int)ret << std::endl;
            //return theCube.status;
        }
        return cube;
    }
}
