#include "Rasterizer.h"
#include "utils.h"

void Rasterizer::emit(void *data)
{
	Batch *bat = static_cast<Batch *>(data);

	rasterizing(bat);
}

void Rasterizer::rasterizing(Batch *bat)
{
	onRasterizing(bat);
}

int Rasterizer::onRasterizing()
{
	return 0;
}

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
	float a1 = (v1.x - xp) * (v2.y - yp) - (v1.y - yp) * (v2.x - xp) * mPrim->mAreaReciprocal;

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

SRHelper * ScanlineRasterizer::createGET(Batch *bat)
{
	DrawContext *dc = bat->mDC;
	PrimBatch &pb = bat->mPrim;
	int mYmin = dc->mRT.height;
	int mYmax = 0;

	PrimBatch::iterator it = pb.begin();
	SRHelper *hlp = new SRHelper();

	hlp->mTri.reserve(pb.size());
	GlobalEdgeTable &get = hlp->mGET;

	//TODO: clipping
	while(it != pb.end())
	{
		Primitive &prim = *it;

		pParent = new triangle(&prim);
		hlp->mTri.push_back(pParent);

		for(size_t i = 0; i < 3; i++)
		{
			const vsOutput &vsout0 = prim.mVert[i];
			const vsOutput &vsout1 = prim.mVert[(i + 1) % 3];
			const vec4 *hvert, *lvert;
			edge *pEdge;
			int ystart;

			int y0 = floor(vsout0.position().y + 0.5f);
			int y1 = floor(vsout1.position().y + 0.5f);

			// regarding horizontal edge, just discard this edge and use the other 2 edges
			if(y0 == y1)
				continue;

			if(y0 > y1)
			{
				hvert = &(vsout0.position());
				lvert = &(vsout1.position());
				ystart = y1;
			}
			else
			{
				hvert = &vert1;
				lvert = &vert0;
				ystart = y0;
			}

			// apply top-left filling convention
			pEdge = new edge(pParent);
			pEdge->dx = (hvert.x - lvert.x) / (hvert.y - lvert.y);
			pEdge->x = lvert.x + ((ystart + 0.5f) - lvert.y) * pEdge->dx;
			pEdge->ymax = floor(hvert.y - 0.5f);

			hlp->ymin = std::min(ystart, mYmin);
			hlp->ymax = std::max(pEdge->ymax, mYmax);

			GlobalEdgeTable::iterator iter = get.find(ystart);
			if(iter == get.end())
				iter = get.insert(pair<int, vector<edge *> >(ystart, vector<edge *>()));

			iter->second.push_back(pEdge);
		}
	}

	return hlp;
}

void ScanlineRasterizer::activateEdgesFromGET(SRHelper *hlp, int y)
{
	GlobalEdgeTable &get = hlp->mGET;
	ActiveEdgeTable &aet = hlp->mAET;
	GlobalEdgeTable::iterator it = get.find(y);

	if(it != get.end())
	{
		vector<edge *> &vGET = it->second;
		
		for(vector<edge *>::iterator it = vGET.begin(); it != vGET.end(); it++)
		{
			(*it)->mParent->setActiveEdge(*it);
			aet.push_back(*it);
		}
	}

	for(vector<edge *>::iterator it = aet.begin(); it != aet.end(); it++)
	{
		(*it)->bActive = true;
	}

	return;
}

// Remove unvisible edges from AET.
void ScanlineRasterizer::removeEdgeFromAET(SRHelper *hlp, int y)
{
	ActiveEdgeTable::iterator it = hlp->mAET.begin();

	while(it != mAET.end())
	{
		if(y > (*it)->ymax)
		{
			(*it)->mParent->unsetActiveEdge(*it);
			it = mAET.erase(it);
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

void ScanlineRasterizer::sortAETbyX()
{
	sort(mAET.begin(), mAET.end(), (static_cast<bool (*)(edge *, edge *)>(this->compareFunc)));
	return;
}

// Perspective-correct interpolation
// Vp/wp = A*(V1/w1) + B*(V2/w2) + C*(V3/w3)
// wp also needs to correct:
// 1/wp = A*(1/w1) + B*(1/w2) + C*(1/w3)
// A, B, C are the barycentric coordinates
void ScanlineRasterizer::interpolate(vec3& coeff, Primitive& prim, fsInput& result)
{
	const vsOutput& v0 = prim.mVert[0];
	const vsOutput& v1 = prim.mVert[1];
	const vsOutput& v2 = prim.mVert[2];

	float coe = 1.0f / dot(coeff, vec3(1.0, 1.0, 1.0));
	result.position().w = coe;

	for(size_t i = 1; i < v0.getRegsNum(); i++)
	{
		const vec4& reg0 = v0.getReg(i);
		const vec4& reg1 = v1.getReg(i);
		const vec4& reg2 = v2.getReg(i);

		result.getReg(i) = vec4(dot(coeff, vec3(reg0.x, reg1.x, reg2.x)),
								dot(coeff, vec3(reg0.y, reg1.y, reg2.y)),
								dot(coeff, vec3(reg0.z, reg1.z, reg2.z)),
								dot(coeff, vec3(reg0.w, reg1.w, reg2.w)));
	}

	return ret;
}

void ScanlineRasterizer::traversalAET(SRHelper *hlp, Batch *bat, int y)
{
	ActiveEdgeTable &aet = hlp->mAET;
	vector<span> vSpans;
	const RenderTarget& rt = bat->mDC->mRT;
	char *colorBuffer = (char *)rt.pColorBuffer;
	
	for(ActiveEdgeTable::iterator it = aet.begin(); it != aet.end(); it++)
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

		pEdge->bActive = false;
		pAdjcentEdge->bActive = false;
	}

	for(vector<span>::iterator it = vSpans.begin(); it < vSpans.end(); it++)
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
				vec3 coeff = bc * (1.0f / pos0.w, 1.0f / pos1.w, 1.0f / pos2.w);

				hlp->in.resize(prim->mVert[0].getRegsNum());
				hlp->in.position() = vec4(x + 0.5f, y + 0.5f, depth, 1.0f);

				interpolate(coeff, *prim, hlp->in);

				getNextStage()->emit(hlp);

				colorBuffer[index+0] = (int)hlp->out.fragcolor().x;
				colorBuffer[index+1] = (int)hlp->out.fragcolor().y;
				colorBuffer[index+2] = (int)hlp->out.fragcolor().z;
				colorBuffer[index+3] = (int)hlp->out.fragcolor().w;
			}
		}
	}

	return;
}

void ScanlineRasterizer::advanceEdgesInAET()
{
	for(vector<edge *>::iterator it = mAET.begin(); it < mAET.end(); it++)
	{
		(*it)->x += (*it)->dx;
	}

	return;

}

void ScanlineRasterizer::scanConversion(SRHelper *hlp, Batch *bat)
{
	for(int i = mYmin; i <= mYmax; i++)
	{
		removeEdgeFromAET(hlp, i);
		activateEdgesFromGET(hlp, i);
		//sortAETbyX();
		traversalAET(hlp, bat, i);
		advanceEdgesInAET();
	}
}

void ScanlineRasterizer::finalize(SRHelper *hlp)
{
	hlp->mAET.clear();

	for(GlobalEdgeTable::iterator it = hlp->mGET.begin(); it != hlp->mGET.end(); it++)
	{
		for(vector<edge *>::iterator iter = it->second.begin(); iter < it->second.end(); iter++)
		{
			delete *iter;
		}
		it->second.clear();
	}
	hlp->mGET.clear();

	for(vector<triangle *>::iterator it = hlp->mTri.begin(); it < hlp->mTri.end(); it++)
		delete *it;

	hlp->mTri.clear();

	delete hlp;

	finalize();
}

int ScanlineRasterizer::onRasterizing()
{
	SRHelper *hlp = createGET(bat);

	scanConversion(hlp, bat);

	finalize();

	return 0;
}

void ScanlineRasterizer::finalize()
{
}

void Rasterizer::finalize()
{
}
