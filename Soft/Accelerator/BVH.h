#ifndef BVH_H
#define BVH_H

#include <algorithm>
#include <stack>
#include <list>

#include "BVHnode.h"

enum class BVHSplitMethod { SAH, Middle, EqualCounts };

template<typename H>
class BVH
{
public:
	struct DepthInfo
	{
		int maxDepth = 0;
		float avgDepth = 0.0f;
	};

public:
	BVH() {}

	BVH(const std::vector<std::shared_ptr<H>> &_hittables, BVHSplitMethod method = BVHSplitMethod::SAH):
		hittables(_hittables), splitMethod(method)
	{
		if (hittables.size() == 0) return;
		std::vector<HittableInfo> hittableInfo(hittables.size());

		AABB bound;
		for (int i = 0; i < hittables.size(); i++)
		{
			AABB box = hittables[i]->bound();
			hittableInfo[i] = { box, box.centroid(), i };
			bound = AABB(bound, box);
		}

		buildRecursive(root, hittableInfo, bound, 0, hittableInfo.size() - 1);

		std::vector<std::shared_ptr<H>> orderedPrims(hittableInfo.size());
		for (int i = 0; i < hittableInfo.size(); i++)
		{
			orderedPrims[i] = hittables[hittableInfo[i].index];
		}

		hittables = orderedPrims;
	}

	~BVH()
	{
		//if (compactNodes != nullptr) delete[] compactNodes;
	}

	void makeCompact()
	{
		if (root == nullptr) return;
		compactNodes = new BVHnodeCompact[treeSize];

		int offset = 0;
		std::stack<BVHnode*> st;
		st.push(root);

		while (!st.empty())
		{
			BVHnode *k = st.top();
			st.pop();

			compactNodes[offset].box = k->box;
			compactNodes[offset].sizeIndex = k->isLeaf() ? -k->offset : k->primCount;
			offset++;

			if (k->rch != nullptr) st.push(k->rch);
			if (k->lch != nullptr) st.push(k->lch);
		}

		destroyRecursive(root);
		std::cout << "[BVH] made compact\n";
	}

	inline std::shared_ptr<H> closestHit(const Ray &ray, float &dist, bool quickCheck)
	{
		if (hittables.size() == 0) return nullptr;
		if (!quickCheck) dist = 1e8f;
		std::shared_ptr<H> hit;
		std::stack<int> st;

		st.push(0);

		while (!st.empty())
		{
			int k = st.top();
			st.pop();
			auto node = compactNodes[k];

			float tpMin, tpMax;
			if (!node.box.hit(ray, tpMin, tpMax)) continue;
			if (tpMin > dist) continue;

			if (node.sizeIndex <= 0)
			{
				auto hitInfo = hittables[-node.sizeIndex]->closestHit(ray);
				if (hitInfo.hit && hitInfo.dist < dist)
				{
					if (quickCheck) return hittables[0];
					hit = hittables[-node.sizeIndex];
					dist = hitInfo.dist;
				}
				continue;
			}

			int lSize = compactNodes[k + 1].sizeIndex;
			if (lSize <= 0) lSize = 1;
			st.push(k + 1 + lSize);
			st.push(k + 1);
		}
		return hit;
	}

	inline void dfs()
	{
		dfs(root, 1);
	}

	inline int size() const { return treeSize; }

	inline DepthInfo dfsDetailed()
	{
		int sumDepth = 0;
		int maxDepth = dfsDetailed(root, 1, sumDepth);
		return { maxDepth, (float)sumDepth / treeSize };
	}

private:
	struct HittableInfo
	{
		AABB bound;
		glm::vec3 centroid;
		int index;
	};

private:
	void buildRecursive(BVHnode *&k, std::vector<HittableInfo> &hittableInfo, const AABB &nodeBound, int l, int r)
	{
		// [l, r]
		int dim = (l == r) ? -1 : nodeBound.maxExtent();
		k = new BVHnode(nodeBound, l, (r - l) * 2 + 1, dim);
		treeSize++;

		//std::cout << l << "  " << r << "  SplitAxis: " << dim << "\n";
		if (l == r) return;

		auto cmp = [dim, this](const HittableInfo &a, const HittableInfo &b)
		{
			return getVec(a.centroid, dim) < getVec(b.centroid, dim);
		};

		std::sort(hittableInfo.begin() + l, hittableInfo.begin() + r, cmp);
		int hittableCount = r - l + 1;

		if (hittableCount == 2)
		{
			buildRecursive(k->lch, hittableInfo, hittableInfo[l].bound, l, l);
			buildRecursive(k->rch, hittableInfo, hittableInfo[r].bound, r, r);
			return;
		}

		AABB *boundInfo = new AABB[hittableCount];
		AABB *boundInfoRev = new AABB[hittableCount];

		boundInfo[0] = hittableInfo[l].bound;
		boundInfoRev[hittableCount - 1] = hittableInfo[r].bound;

		for (int i = 1; i < hittableCount; i++)
		{
			boundInfo[i] = AABB(boundInfo[i - 1], hittableInfo[l + i].bound);
			boundInfoRev[hittableCount - 1 - i] = AABB(boundInfoRev[hittableCount - i], hittableInfo[r - i].bound);
		}

		int m = l;
		switch (splitMethod)
		{
			case BVHSplitMethod::SAH:
				{
					m = l;
					float cost = boundInfo[0].surfaceArea() + boundInfoRev[1].surfaceArea() * (r - l);

					for (int i = 1; i < hittableCount - 1; i++)
					{
						float c = boundInfo[i].surfaceArea() * (i + 1) + boundInfoRev[i + 1].surfaceArea() * (hittableCount - i - 1);
						if (c < cost)
						{
							cost = c;
							m = l + i;
						}
					}
				} break;

			case BVHSplitMethod::Middle:
				{
					glm::vec3 nodeCentroid = nodeBound.centroid();
					float mid = getVec(nodeCentroid, dim);
					for (m = l; m < r - 1; m++)
					{
						float tmp = getVec(hittableInfo[m].centroid, dim);
						if (tmp >= mid) break;
					}
				} break;

			case BVHSplitMethod::EqualCounts:
				m = (l + r) >> 1;
				break;

			default: break;
		}

		AABB lBound = boundInfo[m - l];
		AABB rBound = boundInfoRev[m + 1 - l];
		delete[] boundInfo;
		delete[] boundInfoRev;

		buildRecursive(k->lch, hittableInfo, lBound, l, m);
		buildRecursive(k->rch, hittableInfo, rBound, m + 1, r);
	}

	void destroyRecursive(BVHnode *&k)
	{
		if (k == nullptr) return;
		if (k->lch != nullptr) destroyRecursive(k->lch);
		if (k->rch != nullptr) destroyRecursive(k->rch);
		delete k;
	}

	void dfs(BVHnode *k, int depth)
	{
		if (k == nullptr) return;
		std::cout << depth << "  " << k->offset << " " << k->primCount << " " << k->splitAxis << std::endl;

		if (k->lch != nullptr) dfs(k->lch, depth + 1);
		if (k->rch != nullptr) dfs(k->rch, depth + 1);
	}

	int dfsDetailed(BVHnode *k, int depth, int &sumDepth)
	{
		if (k == nullptr) return 0;

		sumDepth += depth;
		int lDep = dfsDetailed(k->lch, depth + 1, sumDepth);
		int rDep = dfsDetailed(k->rch, depth + 1, sumDepth);

		return std::max(lDep, rDep) + 1;
	}

	float getVec(const glm::vec3 &v, int dim)
	{
		return *(float*)(&v.x + dim);
	}
	
private:
	const int maxHittablesInNode = 1;
	std::vector<std::shared_ptr<H>> hittables;

	BVHnode *root = nullptr;
	int treeSize = 0;
	BVHSplitMethod splitMethod;

	BVHnodeCompact *compactNodes = nullptr;
};

#endif
