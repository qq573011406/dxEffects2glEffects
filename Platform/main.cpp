

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>


#include "hlsl2glsl.h"
// fx parser
#include "driver.h"
#include "expression.h"

std::string ReadFile(const char* fileName)
{
	std::ifstream ifs(fileName);
	if (!ifs)return "";
	std::stringstream buffer;
	buffer << ifs.rdbuf();
	return buffer.str();
}


void printUsage()
{
	std::cout << "Usage:\n dx2gl <source file path>  <output dir>"<<std::endl;
	std::cout << "eg:\n dx2gl c:\\test.hlsl c:\\" << std::endl;
}

struct IncludeContext
{
	std::string currentFolder;
};

static bool C_DECL IncludeOpenCallback(bool isSystem, const char* fname, const char* parentfname, const char* parent, std::string& output, void* d)
{
	const IncludeContext* data = reinterpret_cast<IncludeContext*>(d);

	std::string pathName = data->currentFolder + "/" + fname;

	output = ReadFile(pathName.c_str());
	return true;
}

static std::string GetCompiledShaderText(ShHandle parser)
{
	std::string txt = Hlsl2Glsl_GetShader(parser);

	int count = Hlsl2Glsl_GetUniformCount(parser);
	if (count > 0)
	{
		const ShUniformInfo* uni = Hlsl2Glsl_GetUniformInfo(parser);
		txt += "\n// uniforms:\n";
		for (int i = 0; i < count; ++i)
		{
			char buf[1000];
			snprintf(buf, 1000, "// %s:%s type %d arrsize %d", uni[i].name, uni[i].semantic ? uni[i].semantic : "<none>", uni[i].type, uni[i].arraySize);
			txt += buf;

			if (uni[i].registerSpec)
			{
				txt += " register ";
				txt += uni[i].registerSpec;
			}

			txt += "\n";
		}
	}

	return txt;
}




bool hlsl2glsl(const std::string inputPath,const std::string& inCode,const std::string& enterpoint,EShLanguage toLang, ETargetVersion toVersion,std::string& outCode)
{

	// Include Handle
	IncludeContext includeCtx;
	includeCtx.currentFolder = inputPath.substr(0, inputPath.rfind('/'));
	Hlsl2Glsl_ParseCallbacks includeCB;
	includeCB.includeOpenCallback = IncludeOpenCallback;
	includeCB.includeCloseCallback = NULL;
	includeCB.data = &includeCtx;

	ShHandle parser = Hlsl2Glsl_ConstructCompiler(toLang);

	const char* sourceStr = inCode.c_str();
	const char* infoLog = nullptr;

	int parseOk = Hlsl2Glsl_Parse(parser, sourceStr, toVersion, &includeCB, 0);
	if (!parseOk) {
		infoLog = Hlsl2Glsl_GetInfoLog(parser);
		std::cerr << infoLog << std::endl;
		Hlsl2Glsl_DestructCompiler(parser);
		return false;

	}
	int translateOk = Hlsl2Glsl_Translate(parser,enterpoint.c_str(), toVersion, 0);
	if (!translateOk) {
		infoLog = Hlsl2Glsl_GetInfoLog(parser);
		std::cerr << infoLog << std::endl;
		Hlsl2Glsl_DestructCompiler(parser);
		return false;
	}

	outCode = GetCompiledShaderText(parser);
	Hlsl2Glsl_DestructCompiler(parser);
	return true;
}


std::string get_file_name(std::string path)
{
	size_t pos = path.find_last_of('/');
	if (pos < 0) {
		pos = path.find_first_of('\\');
	}
	if (pos < 0)return path;

	return path.substr(pos + 1);
}


bool translate_hlfx_to_glfx(const std::string& fx_in_path,const std::string glfx_out_dir)
{
	
	// 解析fx 文件
	DxEffectsTree fxTree;
	example::Driver driver(fxTree);
	if (!driver.parse_file(fx_in_path)) {
		return false;
	}

	auto code_block = fxTree.getCodeBlock();
	
	// 初始化HLSL2GLSLFORK
	Hlsl2Glsl_Initialize();



	std::stringstream glslCodeBlockOut;
	std::stringstream esslCodeBlockOut;

	auto techniques = fxTree.getTechiques();

	std::stringstream techout;
	for (auto technique : techniques)
	{

		techout << "technique " << technique->getName() << std::endl;
		techout << "{" << std::endl;

		auto passes = technique->getPasses();	
		std::cout << "Parse Technique:" + technique->getName() << " Total Pass:" <<passes.size()<< std::endl;
		for (auto pass:passes)
		{
			std::stringstream passout;
			techout << "\tpass " << pass->getName() << std::endl;
			techout << "\t{" << std::endl;

			auto states = pass->getStateAssignments();
			std::cout << "Parse Pass:" + pass->getName() << " Total StateAssignment:" << states.size() << std::endl;
			for (auto state:states)
			{
				std::stringstream stateOut;
				stateOut << "\t\t" << state->getName();
				if (state->getNameIndex() != -1) {
					stateOut << "[" << state->getNameIndex() << "]";
				}
				stateOut << " = ";
				auto stateValue = state->getValue();
				auto stateValueType = stateValue->getValueType();
				switch (stateValueType)
				{
				case StateValueType::COMPILE:
				{
					auto value = static_cast<const StateCompileValue*>(stateValue);
					auto target = value->getTarget();
					auto enterpoint = value->getEntryPoint();
					EShLanguage toLang = EShLangCount;
					if (target.find("vs_") != std::string::npos) {
						toLang = EShLangVertex;
						stateOut << "compile vs " << enterpoint << "()";
					}
					else if (target.find("ps_") != std::string::npos) {
						toLang = EShLangFragment;
						stateOut << "compile ps " << enterpoint << "()";
					}
					else
					{
						std::cerr << "unkonw compile target " << target << std::endl;
						Hlsl2Glsl_Shutdown();
						return false;
					}
	
					std::string glslCode;
					std::string esslCode;
					bool ret = false;
					ret = hlsl2glsl(fx_in_path, code_block, enterpoint, toLang, ETargetVersion::ETargetGLSL_140, glslCode);
					if (!ret) {
						Hlsl2Glsl_Shutdown();
						return false;
					}
					ret = hlsl2glsl(fx_in_path, code_block, enterpoint, toLang, ETargetVersion::ETargetGLSL_ES_300, esslCode);
					if (!ret) {
						Hlsl2Glsl_Shutdown();
						return false;
					}

					glslCodeBlockOut << "#CODEBLOCK_BEGIN" << std::endl;
					glslCodeBlockOut << glslCode << std::endl;
					glslCodeBlockOut << "#CODEBLOCK_END " << enterpoint << "\n\n\n";

					esslCodeBlockOut << "#CODEBLOCK_BEGIN" << std::endl;
					esslCodeBlockOut << glslCode << std::endl;
					esslCodeBlockOut << "#CODEBLOCK_END " << enterpoint << "\n\n\n";
					
				}
				break;
				case StateValueType::BOOLEAN:
					stateOut << stateValue->toString();
					break;
				case StateValueType::STRING:
					stateOut << stateValue->toString();
					break;
				case StateValueType::INTEGER:
					stateOut << stateValue->toString();
					break;
				case StateValueType::FLOAT:
					stateOut << stateValue->toString();
					break;
				case StateValueType::FLOAT2:
					stateOut << stateValue->toString();
					break;
				case StateValueType::FLOAT3:
					stateOut << stateValue->toString();
					break;
				case StateValueType::FLOAT4:
					stateOut << stateValue->toString();
					break;
				case StateValueType::D3DCOLOR:
					stateOut << stateValue->toString();
					break;
				case StateValueType::UNKNOWN:
					std::cerr << "WARNING: Unkonw State type " << state->getName();
					break;
				default:
					std::cerr << "WARNING: Unkonw State type " << state->getName();
					break;
				}
				stateOut << ";";
				passout << stateOut.str() << std::endl;
			}

			passout << "\t}";

			techout << passout.str() << std::endl;
		}

		techout << "}" << std::endl;
	}
	
	// write to file
	auto fileName = get_file_name(fx_in_path);
	auto glslOutPath = glfx_out_dir + "/" + fileName + ".glsl";
	auto glesOutPath = glfx_out_dir + "/" + fileName + ".essl";
	
	std::ofstream glslOf;
	glslOf.open(glslOutPath);
	if (!glslOf.is_open()) {
		std::cerr << "can't open to write " + glslOutPath << std::endl;
		return false;
		Hlsl2Glsl_Shutdown();
	}
	glslOf << glslCodeBlockOut.str() << std::endl << techout.str();
	glslOf.close();

	std::ofstream esslOf;
	esslOf.open(glesOutPath);
	if (!esslOf.is_open()) {
		std::cerr << "can't open to write " + glesOutPath << std::endl;
		return false;
		Hlsl2Glsl_Shutdown();
	}
	esslOf << esslCodeBlockOut.str() << std::endl << techout.str();
	esslOf.close();


	Hlsl2Glsl_Shutdown();
	return true;
}




int main(int argc,char** argv)
{
	if (argc < 2)
	{
		printUsage();
		return 0;
	}
	std::string inputPath = argv[1];
	std::string outputDir = argv[2];

	if (translate_hlfx_to_glfx(inputPath, outputDir)) {
		std::cout << "\bSucessed!\n";
	}
	else {
		std::cout << "\bFailed!\n";
	}

	std::system("pause");
	return 0;
}

