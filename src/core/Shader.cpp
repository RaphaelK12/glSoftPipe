#include "Shader.h"

void VertexShader::compile()
{
	// No need to compile for embed shader
}

void VertexShader::execute()
{
	std::cout << __func__ << ": Please insert the code you want to excute!" << std::endl;
}

int VertexShader::SetVertexCount(unsigned int count)
{
	mVertexCount = count;
	return 0;
}

int VertexShader::SetAttribCount(unsigned int count)
{
	mAttribCount = count;
	return 0;
}

int VertexShader::SetVaryingCount(unsigned int count)
{
	mVaryingCount = count;
	return 0;
}

int VertexShader::AttribPointer(int index, void *ptr)
{
	mIn[index] = ptr;
	return 0;
}

int VertexShader::VaryingPointer(int index, void *ptr)
{
	mOut[index] = ptr;
	return 0;
}

void PixelShader::compile()
{
}

void PixelShader::attribPointer(float *attri)
{
	mIn = attri;
}

void PixelShader::setupOutputRegister(char *outReg)
{
	mOutReg = outReg;
}

// TODO: Inherit PixelShader to do concrete implementation
void PixelShader::execute()
{
	mOutReg[0] = (int)mIn[0];
	mOutReg[1] = (int)mIn[1];
	mOutReg[2] = (int)mIn[2];
	mOutReg[3] = (int)mIn[3];
}
