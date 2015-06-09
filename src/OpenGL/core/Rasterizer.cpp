#include "Rasterizer.h"

#include <iostream>
#include <vector>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cfloat>	
#include "DrawEngine.h"
#include "GLContext.h"
#include "utils.h"


NS_OPEN_GLSP_OGL()

using glm::vec3;
using glm::vec4;

RasterizerWrapper::RasterizerWrapper():
	PipeStage("Rasterizier Wrapper", DrawEngine::getDrawEngine()),
	mpRasterizer(new ScanlineRasterizer),
	mpInterpolate(new PerspectiveCorrectInterpolater),
	// RasterizerWrapper doesn't own FS,
	// will be linked in when draw validate.
	mpFS(NULL),
	mpOwnershipTest(new OwnershipTester),
	mpScissorTest(new ScissorTester),
	mpAlphaTest(new AlphaTester),
	mpStencilTest(new StencilTester),
	mpDepthTest(new ZTester),
	mpBlender(new Blender),
	mpDither(new Dither)
{
	setFirstStage(mpRasterizer);

	Interpolater *interp = dynamic_cast<Interpolater *>(mpInterpolate);
	assert(interp);

	mpRasterizer->SetInterpolater(mpInterpolate);
}

RasterizerWrapper::~RasterizerWrapper()
{
	delete mpDither;
	delete mpBlender;
	delete mpDepthTest;
	delete mpStencilTest;
	delete mpAlphaTest;
	delete mpScissorTest;
	delete mpOwnershipTest;
	delete mpInterpolate;
	delete mpRasterizer;
}

void RasterizerWrapper::emit(void *data)
{
	Batch *bat = static_cast<Batch *>(data);
	Primlist &pl = bat->mPrims;
		
	Interpolater *interp = dynamic_cast<Interpolater *>(mpInterpolate);
	assert(interp);

	getFirstStage()->emit(bat);
}

void RasterizerWrapper::finalize()
{
}

void Interpolater::emit(void *data)
{
	onInterpolating();
}

void Interpolater::finalize()
{
}

Rasterizer::Rasterizer():
	PipeStage("Rasterizing", DrawEngine::getDrawEngine())
{
}

Rasterizer::emit(void *data)
{
	Batch *bat = static_cast<Batch *>(data);

	onRasterizing(bat);
}

void Rasterizer::finalize()
{
}


// active edge table implementation
// TODO: tile-based implementation
class ScanlineRasterizer: public Rasterizer
{
public:
	ScanlineRasterizer(): Rasterizer()
	{
	}

	virtual ~ScanlineRasterizer() { }

	void SetInterpolater(Interpolater *interp)
	{
		mpInterpolate = interp;
	}

protected:
	virtual int onRasterizing(Batch *bat);

private:
	class edge;
	class triangle;
	class span;
	class SRHelper;

	static bool compareFunc(edge *pEdge1, edge *pEdge2);

	SRHelper* createGET(Batch *bat);
	void scanConversion(SRHelper *hlp, Batch *bat);
	void activateEdgesFromGET(SRHelper *hlp, int y);
	void removeEdgeFromAET(SRHelper *hlp, int y);
	//void sortAETbyX();

	// perspective-correct interpolation
	void interpolate(glm::vec3 &coeff, Primitive& prim, fsInput& result);
	void traversalAET(SRHelper *hlp, Batch *bat, int y);
	void advanceEdgesInAET(SRHelper *hlp);

	void finalize(SRHelper *hlp);

	// Used to calculate gradiences.
	Interpolater *mpInterpolate;
};

class ScanlineRasterizer::Gradience
{
public:
	Gradience(Primitive &prim);
	~Gradience() { }

	float mOneOverZStarts[Primitive::MAX_PRIM_TYPE];
	std::vector<vec4>	mStarts[Primitive::MAX_PRIM_TYPE];

	std::vector<vec4>	mGradiences;
	Primitive		   *mPrim;
};

// TBC
ScanlineRasterizer::Gradience::Gradience(Primitive &prim)
{
	for(int i = 0; i < prim.mVertNum; i++)
	{
		mOneOverZStarts[i] = 1.0f / prim.mVert[i].position().w;
		size_t size = prim.mVert[i].getRegsNum() - 1;
		mStarts[i].resize(size);

		for(size_t j = 0; j < size; j++)
		{
			mStarts[i][j] = prim.mVert[i].getReg(j+1);
		}
	}
}

class ScanlineRasterizer::triangle
{
public:
	triangle(Primitive *prim):
		mActiveEdge0(NULL),
		mActiveEdge1(NULL),
		mPrim(prim)
	{
	}
	~triangle() {}

	vec3 calculateBC(float xp, float yp);
	void setActiveEdge(edge *pEdge);
	void unsetActiveEdge(edge *pEdge);
	edge* getAdjcentEdge(edge *pEdge);

private:
	// There can be at most two edges in AET at the same time
	edge *mActiveEdge0;
	edge *mActiveEdge1;
	Primitive *mPrim;
};

// Calculate the barycentric coodinates of point P in this triangle
//
// 1. use homogeneous coordinates
// [x0  x1  x2 ] -1    [xp ]
// [y0  y1  y2 ]    *  [yp ]
// [1.0 1.0 1.0]       [1.0]
//
// 2. use area
vec3 ScanlineRasterizer::triangle::calculateBC(float xp, float yp)
{
	const vec4& v0 = mPrim->mVert[0].position();
	const vec4& v1 = mPrim->mVert[1].position();
	const vec4& v2 = mPrim->mVert[2].position();

	float a0 = ((v1.x - xp) * (v2.y - yp) - (v1.y - yp) * (v2.x - xp)) * mPrim->mAreaReciprocal;
	float a1 = ((v2.x - xp) * (v0.y - yp) - (v2.y - yp) * (v0.x - xp)) * mPrim->mAreaReciprocal;

	return vec3(a0, a1, 1.0f - a0 - a1);
	//return vec3(mMatrix * vec3(xp, yp, 1.0));
}

void ScanlineRasterizer::triangle::setActiveEdge(edge *pEdge)
{
	if(!mActiveEdge0)
	{
		mActiveEdge0 = pEdge;
	}
	else if(!mActiveEdge1)
	{
		mActiveEdge1 = pEdge;
	}
	else
	{
		cout << "How could this happen!" << endl;
		assert(0);
	}
}

void ScanlineRasterizer::triangle::unsetActiveEdge(edge *pEdge)
{
	if(pEdge == mActiveEdge0)
		mActiveEdge0 = NULL;
	else if(pEdge == mActiveEdge1)
		mActiveEdge1 = NULL;
	else
	{
		cout << "This edge is not active" << endl;
	}
}

ScanlineRasterizer::edge* ScanlineRasterizer::triangle::getAdjcentEdge(edge *pEdge)
{
	if(pEdge == mActiveEdge0)
		return mActiveEdge1;

	if(pEdge == mActiveEdge1)
		return mActiveEdge0;

	cout << "How could this happen!" << endl;
	assert(0);
}

class ScanlineRasterizer::edge
{
public:
	edge(triangle *pParent):
		mParent(pParent),
		bActive(false) 
	{
	}
	~edge() {}

	float x;
	float dx;
	int ymax;
	triangle *mParent;
	bool bActive;
};


class ScanlineRasterizer::span
{
public:
	span(float x1, float x2, triangle *pParent):
		xleft(x1),
		xright(x2),
		mParent(pParent)
	{
	}
	~span() {}

	float xleft;
	float xright;
	triangle *mParent;
};

class ScanlineRasterizer::SRHelper
{
public:
	int ymin;
	int ymax;
	std::vector<triangle *> mTri;

	typedef std::unordered_map<int, std::vector<edge *> > GlobalEdgeTable;
	typedef std::list<edge *> ActiveEdgeTable;
	// global edges table
	GlobalEdgeTable mGET;
	// active edges table
	ActiveEdgeTable mAET;

	// used in following stages, e.g. fragment shader, merge.
	Rasterizer::fs_in_out mFsio;
};

ScanlineRasterizer::SRHelper* ScanlineRasterizer::createGET(Batch *bat)
{
	GLContext *gc = bat->mDC->gc;
	Primlist  &pl = bat->mPrims;

	SRHelper *hlp = new SRHelper();

	hlp->ymin = gc->mRT.height;
	hlp->ymax = 0;

	hlp->mTri.reserve(pl.size());
	auto &get = hlp->mGET;

	//TODO: clipping
	for(auto it = pl.begin(); it != pl.end(); it++)
	{
		Primitive &prim = *it;

		triangle *pParent = new triangle(&prim);
		hlp->mTri.push_back(pParent);

		for(size_t i = 0; i < 3; i++)
		{
			vsOutput &vsout0 = prim.mVert[i];
			vsOutput &vsout1 = prim.mVert[(i + 1) % 3];
			const vec4 *hvert, *lvert;
			int ystart;

			int y0 = floor(vsout0.position().y + 0.5f);
			int y1 = floor(vsout1.position().y + 0.5f);

			// regarding horizontal edge, just discard this edge and use the other 2 edges
			if(y0 == y1)
				continue;

			if(y0 > y1)
			{
				hvert  = &(vsout0.position());
				lvert  = &(vsout1.position());
				ystart = y1;
			}
			else
			{
				hvert  = &(vsout1.position());
				lvert  = &(vsout0.position());
				ystart = y0;
			}

			// apply top-left filling convention
			edge *pEdge = new edge(pParent);

			pEdge->dx   = (hvert->x - lvert->x) / (hvert->y - lvert->y);
			pEdge->x    = lvert->x + ((ystart + 0.5f) - lvert->y) * pEdge->dx;
			pEdge->ymax = floor(hvert->y - 0.5f);

			hlp->ymin = std::min(ystart, hlp->ymin);
			hlp->ymax = std::max(pEdge->ymax, hlp->ymax);

			auto iter = get.find(ystart);
			if(iter == get.end())
				//get.insert(pair<int, vector<edge *> >(ystart, vector<edge *>()));
				get[ystart] = vector<edge *>();

			get[ystart].push_back(pEdge);
		}

		mpInterpolate->CalculateRadiences()
	}

	return hlp;
}

void ScanlineRasterizer::activateEdgesFromGET(SRHelper *hlp, int y)
{
	auto &get = hlp->mGET;
	auto &aet = hlp->mAET;
	auto it = get.find(y);

	if(it != get.end())
	{
		auto &vGET = it->second;
		
		std::for_each(vGET.begin(), vGET.end(),
					[&aet] (edge *pEdge)
					{
						pEdge->mParent->setActiveEdge(pEdge);
						aet.push_back(pEdge);
					});
	}

	for(auto it = aet.begin(); it != aet.end(); it++)
	{
		(*it)->bActive = true;
	}

	return;
}

// Remove unvisible edges from AET.
void ScanlineRasterizer::removeEdgeFromAET(SRHelper *hlp, int y)
{
	auto &aet = hlp->mAET;
	auto it   = aet.begin();

	while(it != aet.end())
	{
		if(y > (*it)->ymax)
		{
			(*it)->mParent->unsetActiveEdge(*it);
			it = aet.erase(it);
		}
		else
		{
			it++;
		}
	}

	return;
}

bool ScanlineRasterizer::compareFunc(edge *pEdge1, edge *pEdge2)
{
	return (pEdge1->x <= pEdge2->x);
}

#if 0
void ScanlineRasterizer::sortAETbyX()
{
	sort(mAET.begin(), mAET.end(), (static_cast<bool (*)(edge *, edge *)>(this->compareFunc)));
	return;
}
#endif

// Perspective-correct interpolation
// Vp/wp = A*(V1/w1) + B*(V2/w2) + C*(V3/w3)
// wp also needs to correct:
// 1/wp = A*(1/w1) + B*(1/w2) + C*(1/w3)
// A, B, C are the barycentric coordinates
void ScanlineRasterizer::interpolate(vec3& coeff, Primitive& prim, fsInput& result)
{
	vsOutput& v0 = prim.mVert[0];
	vsOutput& v1 = prim.mVert[1];
	vsOutput& v2 = prim.mVert[2];

	float coe = 1.0f / dot(coeff, vec3(1.0, 1.0, 1.0));
	result.position().w = coe;

	for(size_t i = 1; i < v0.getRegsNum(); i++)
	{
		const vec4& reg0 = v0.getReg(i);
		const vec4& reg1 = v1.getReg(i);
		const vec4& reg2 = v2.getReg(i);

		result.getReg(i) = vec4(dot(coeff, vec3(reg0.x, reg1.x, reg2.x)) * coe,
								dot(coeff, vec3(reg0.y, reg1.y, reg2.y)) * coe,
								dot(coeff, vec3(reg0.z, reg1.z, reg2.z)) * coe,
								dot(coeff, vec3(reg0.w, reg1.w, reg2.w)) * coe);
	}
}

void ScanlineRasterizer::traversalAET(SRHelper *hlp, Batch *bat, int y)
{
	auto &aet = hlp->mAET;
	std::vector<span> vSpans;
	const RenderTarget& rt = bat->mDC->gc->mRT;
	unsigned char *colorBuffer = (unsigned char *)rt.pColorBuffer;
	auto &fsio = hlp->mFsio;
	
	for(auto it = aet.begin(); it != aet.end(); it++)
	{
		if((*it)->bActive != true)
			continue;

		edge *pEdge = *it;
		triangle *pParent = pEdge->mParent;
		edge *pAdjcentEdge = pParent->getAdjcentEdge(pEdge);

		assert(pAdjcentEdge->bActive);

		float xleft = fmin(pEdge->x, pAdjcentEdge->x);
		float xright = fmax(pEdge->x, pAdjcentEdge->x);
		vSpans.push_back(span(xleft, xright, pParent));

		pEdge->bActive        = false;
		pAdjcentEdge->bActive = false;
	}

	for(auto it = vSpans.begin(); it < vSpans.end(); it++)
	{
		// top-left filling convention
		for(int x = ceil(it->xleft - 0.5f); x < ceil(it->xright - 0.5f); x++)
		{
			int index = (rt.height - y - 1) * rt.width + x;
			triangle *pParent = it->mParent;
			Primitive *prim = pParent->mPrim;

			const vec4& pos0 = prim->mVert[0].position();
			const vec4& pos1 = prim->mVert[1].position();
			const vec4& pos2 = prim->mVert[2].position();

			// There is no need to do perspective-correct for z.
			vec3 bc = pParent->calculateBC(x + 0.5f, y + 0.5f);
			float depth = dot(bc, vec3(pos0.z, pos1.z, pos2.z));

			// Early-z implementation, so draw near objs first will gain more performance.
			// TODO: "defer" implementation
			if(depth < rt.pDepthBuffer[index])
			{
				rt.pDepthBuffer[index] = depth;
				vec3 coeff = bc * vec3(1.0f / pos0.w, 1.0f / pos1.w, 1.0f / pos2.w);

				fsio.in.resize(prim->mVert[0].getRegsNum());
				fsio.in.position() = vec4(x + 0.5f, y + 0.5f, depth, 1.0f);

				interpolate(coeff, *prim, fsio.in);

				getNextStage()->emit(&fsio);

				colorBuffer[4 * index+2] = (unsigned char)(fsio.out.fragcolor().x * 256);
				colorBuffer[4 * index+1] = (unsigned char)(fsio.out.fragcolor().y * 256);
				colorBuffer[4 * index+0] = (unsigned char)(fsio.out.fragcolor().z * 256);
				//colorBuffer[4 * index+3] = (unsigned char)(fsio.out.fragcolor().w * 256);
				colorBuffer[4 * index+3] = 255;
			}
			else
			{
			}
		}
	}

	return;
}

void ScanlineRasterizer::advanceEdgesInAET(SRHelper *hlp)
{
	for(auto it = hlp->mAET.begin(); it != hlp->mAET.end(); it++)
	{
		(*it)->x += (*it)->dx;
	}

	return;

}

void ScanlineRasterizer::scanConversion(SRHelper *hlp, Batch *bat)
{
	for(int i = hlp->ymin; i <= hlp->ymax; i++)
	{
		removeEdgeFromAET(hlp, i);
		activateEdgesFromGET(hlp, i);
		//sortAETbyX();
		traversalAET(hlp, bat, i);
		advanceEdgesInAET(hlp);
	}
}

void ScanlineRasterizer::finalize(SRHelper *hlp)
{
	hlp->mAET.clear();

	for(auto it = hlp->mGET.begin(); it != hlp->mGET.end(); it++)
	{
		for(auto iter = it->second.begin(); iter < it->second.end(); iter++)
		{
			delete *iter;
		}
		it->second.clear();
	}
	hlp->mGET.clear();

	for(auto it = hlp->mTri.begin(); it < hlp->mTri.end(); it++)
		delete *it;

	hlp->mTri.clear();

	delete hlp;

	finalize();
}

int ScanlineRasterizer::onRasterizing(Batch *bat)
{
	SRHelper *hlp = createGET(bat);

	scanConversion(hlp, bat);

	finalize(hlp);

	return 0;
}

class PerspectiveCorrectInterpolater: public Interpolater
{
public:
	PerspectiveCorrectInterpolater() { }
	~PerspectiveCorrectInterpolater() { }

	virtual void CalculateRadiences(Primitive &prim);

private:
	virtual void onInterpolating(const vsOutput &vo,
								 const Gradience &grad;
								 float stepx, float stepy,
								 fsInput &result);
};

void PerspectiveCorrectInterpolater::CalculateRadiences(Primitive &prim)
{

}
NS_CLOSE_GLSP_OGL()
