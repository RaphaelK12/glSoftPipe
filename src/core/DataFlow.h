#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <list>
#include <unordered_map>

typedef std::vector<glm::vec4> RegArray;

class ShaderRegisterFile
{
public:
	// getReg() and resize is one pair
	void resize(size_t n)
	{
		mRegs.resize(n);
	}
	glm::vec4 & getReg(int location)
	{
		assert(location < (int)mRegs.size());
		return mRegs[location];
	}

	// glPosition and FragColor are both in the first location,
	// which can be treat as a union.
	glm::vec4 & position()
	{
		return mRegs[0];
	}
	glm::vec4 & fragcolor()
	{
		return mRegs[0];
	}

	// reserve() and assemble() is one pair
	void reserve(size_t n)
	{
		mRegs.reserve(n);
	}

	void pushReg(const glm::vec4 &attr)
	{
		mRegs.push_back(attr);
	}

	size_t getRegsNum() const
	{
		return mRegs.size();
	}

private:
	RegArray mRegs;
};

typedef ShaderRegisterFile vsInput;
typedef ShaderRegisterFile vsOutput;

typedef ShaderRegisterFile fsInput;
typedef ShaderRegisterFile fsOutput;

struct Primitive
{
	enum PrimType
	{
		POINT = 0,
		LINE,
		TRIANGLE
	};
	
	PrimType mType;

#if PRIMITIVE_REFS_VERTICES
	vsOutput *mVert[3];
#elif PRIMITIVE_OWNS_VERTICES
	vsOutput mVert[3];
#endif
	float mAreaReciprocal;
};

typedef std::vector<int> IBuffer_v;
typedef std::unordered_map<int, int> vsCacheIndex;
typedef std::vector<vsInput> vsCache;
typedef std::list<Primitive> PrimBatch;

#if PRIMITIVE_REFS_VERTICES
typedef std::vector<vsOutput *> vsOutput_v;
#elif PRIMITIVE_OWNS_VERTICES
typedef std::vector<vsOutput> vsOutput_v;
#endif

// TODO: comment
// Batch represents a batch of data flow to be passed through the whole pipeline
// It's hard to give a decent name to each member based on their repective usages.
// Here is rough explanation:
// Input Assembly: read data from VBO, produce mVertexCache & mIndexBuf.
// Vertex Shading: consumer mVertexCache, produce mVsOut
// Primitive Assembly: consumer mIndexBuf & mVsOut, produce mPrims
// Clipping ~ Viewport transform: consumer mPrims, produce mPrims
struct Batch
{
	vsCache			mVertexCache;
	vsCacheIndex	mCacheIndex;
	vsOutput_v		mVsOut;
	IBuffer_v		mIndexBuf;
	PrimBatch		mPrims;

	// point back to the mDC
	DrawContext	   *mDC;
};
