// Copyright (C) 2025 VOPI Team
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "irrTypes.h"
#include "vector3d.h"
#include "IVertexBuffer.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace scene
{

//! CPU-side morph-target (glTF blend shape / Blender shape key) data for a mesh buffer.
/** Variant B pipeline: the weighted sum of the per-target vertex deltas is added
to the base ("rest") vertices on the CPU each frame, producing a morphed rest pose
that the existing (hardware or software) skinning then consumes. The order is
always morph -> skin. No shader changes are required.

A buffer either carries an animated morph (a non-empty WeightChannel, applied every
frame via apply()) or a static morph (no channel; baked once into the vertex buffer
via bakeStatic() at load time and then dropped). */
struct MorphBuffer final
{
	//! A single morph target: per-vertex position (and optional normal) deltas.
	struct Target
	{
		//! Position deltas, one per vertex (size == vertex count), or empty
		//! when the target declares no POSITION accessor.
		std::vector<core::vector3df> positions;
		//! Normal deltas, present iff the target provides NORMAL deltas.
		std::optional<std::vector<core::vector3df>> normals;
	};

	//! Vector-valued animation of the target weights over time.
	/** glTF stores absolute weights (not additive deltas). The output is row-major:
	values[frame * numTargets + target]. */
	struct WeightChannel
	{
		std::vector<f32> times;  //!< keyframe times (numFrames)
		std::vector<f32> values; //!< numFrames * numTargets weights
		bool interpolate = true; //!< LINEAR (true) or STEP (false)

		bool empty() const { return times.empty(); }
		f32 getEndFrame() const { return times.empty() ? 0.0f : times.back(); }

		//! Samples the weights at the given frame into out (resized to numTargets).
		void sample(f32 frame, std::size_t numTargets, std::vector<f32> &out) const;
	};

	std::vector<Target> targets;
	WeightChannel channel;        //!< empty() => static morph (baked at load)
	std::vector<f32> baseWeights; //!< fallback/default weights (size == numTargets)

	//! Number of targets declared in the source file. May exceed
	//! numTargets() when the loader capped the stored targets; the weights
	//! animation data in the file is laid out with this stride.
	std::size_t declaredTargets = 0;

	std::size_t numTargets() const { return targets.size(); }
	bool hasAnimation() const { return !channel.empty(); }
	f32 getEndFrame() const { return channel.getEndFrame(); }

	//! Largest per-vertex displacement magnitude assuming all weights are 1.
	//! Used together with maxAbsWeight() for a conservative bounding-box margin.
	f32 maxDisplacement() const;

	//! Largest absolute weight across base weights and the animation channel.
	f32 maxAbsWeight() const;

	//! Applies base + sum(w * delta) for the given frame into vbuf.
	//! Captures the rest pose lazily on the first call.
	void apply(IVertexBuffer *vbuf, f32 frame);

	//! Bakes a static (channel-less) morph using baseWeights directly into vbuf.
	//! Used at finalize for non-animated morphs; keeps no runtime state.
	void bakeStatic(IVertexBuffer *vbuf) const;

private:
	struct RestVertex
	{
		core::vector3df pos;
		core::vector3df normal;
	};

	//! True if any target carries normal deltas.
	bool hasNormalDeltas() const;

	//! Computes base + sum(w * delta) for one vertex. Shared by apply()/bakeStatic().
	//! If doNormals and the morphed normal collapses to ~zero, keeps restNormal.
	void computeMorphedVertex(std::size_t v, const std::vector<f32> &weights,
			bool doNormals, const core::vector3df &restPos,
			const core::vector3df &restNormal, core::vector3df &outPos,
			core::vector3df &outNormal) const;

	void captureRest(const IVertexBuffer *vbuf);

	std::unique_ptr<RestVertex[]> m_rest; //!< lazy snapshot of the base pose
	std::size_t m_rest_count = 0;
	bool m_morph_normals = false;         //!< any target carries normal deltas
	std::vector<f32> m_weights;           //!< per-frame sampling scratch
};

} // end namespace scene
