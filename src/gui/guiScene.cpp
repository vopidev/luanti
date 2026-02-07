// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2020 Jean-Patrick Guerrero <jeanpatrick.guerrero@gmail.com>

#include "guiScene.h"

#include <SViewFrustum.h>
#include <AnimatedMeshSceneNode.h>
#include <IVideoDriver.h>
#include <ISceneManager.h>
#include "porting.h"
#include "client/mesh.h"

GUIScene::GUIScene(gui::IGUIEnvironment *env, scene::ISceneManager *smgr,
		   gui::IGUIElement *parent, core::recti rect, s32 id)
	: IGUIElement(gui::EGUIET_ELEMENT, env, parent, id, rect)
{
	m_driver = env->getVideoDriver();
	m_smgr = smgr->createNewSceneManager(false);

	m_cam = m_smgr->addCameraSceneNode(0, v3f(0.f, 0.f, -100.f), v3f(0.f));
	m_cam->setFOV(30.f * core::DEGTORAD);
}

GUIScene::~GUIScene()
{
	setMesh(nullptr);

	m_smgr->drop();
}

scene::AnimatedMeshSceneNode *GUIScene::setMesh(scene::IAnimatedMesh *mesh)
{
	if (m_mesh) {
		m_mesh->remove();
		m_mesh = nullptr;
	}

	if (!mesh)
		return nullptr;

	m_mesh = m_smgr->addAnimatedMeshSceneNode(mesh);
	m_mesh->setPosition(-m_mesh->getBoundingBox().getCenter());
	m_mesh->animateJoints();

#if IS_VOPI_ENGINE
	// Fix vertex colors: ensure all vertices are white so textures
	// display at full brightness instead of being darkened.
	// Applied to scene node's mesh copy, not the shared source mesh.
	{
		scene::IMesh *node_mesh = m_mesh->getMesh();
		if (node_mesh) {
			video::SColor white(255, 255, 255, 255);
			for (u32 i = 0; i < node_mesh->getMeshBufferCount(); ++i) {
				scene::IMeshBuffer *buf = node_mesh->getMeshBuffer(i);
				if (!buf)
					continue;
				const u32 stride = getVertexPitchFromType(buf->getVertexType());
				u8 *vertices = (u8 *)buf->getVertices();
				for (u32 j = 0; j < buf->getVertexCount(); ++j)
					((video::S3DVertex *)(vertices + j * stride))->Color = white;
				buf->setDirty(scene::EBT_VERTEX);
			}
		}
	}
#endif

	return m_mesh;
}

void GUIScene::setTexture(u32 idx, video::ITexture *texture)
{
	video::SMaterial &material = m_mesh->getMaterial(idx);
	material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	material.MaterialTypeParam = 0.5f;
	material.TextureLayers[0].Texture = texture;
	material.FogEnable = true;
	material.TextureLayers[0].MinFilter = video::ETMINF_NEAREST_MIPMAP_NEAREST;
	material.TextureLayers[0].MagFilter = video::ETMAGF_NEAREST;
	material.BackfaceCulling = false;
	material.ZWriteEnable = video::EZW_AUTO;
}

void GUIScene::draw()
{
	m_driver->clearBuffers(video::ECBF_DEPTH);

	// Control rotation speed based on time
	u64 new_time = porting::getTimeMs();
	u64 dtime_ms = 0;
	if (m_last_time != 0)
		dtime_ms = porting::getDeltaMs(m_last_time, new_time);
	m_last_time = new_time;

	core::rect<s32> oldViewPort = m_driver->getViewPort();
	m_driver->setViewPort(getAbsoluteClippingRect());

	if (m_bgcolor != 0) {
		core::recti borderRect =
				Environment->getRootGUIElement()->getAbsoluteClippingRect();
		Environment->getSkin()->draw3DSunkenPane(
			this, m_bgcolor, false, true, borderRect, 0);
	}

	core::dimension2d<s32> size = getAbsoluteClippingRect().getSize();
	m_smgr->getActiveCamera()->setAspectRatio((f32)size.Width / (f32)size.Height);

	if (!m_target) {
		updateCamera(m_smgr->addEmptySceneNode());
		rotateCamera(v3f(0.f));
		m_cam->bindTargetAndRotation(true);
	}

#if IS_VOPI_ENGINE
	// Apply initial rotation and distance before drawing so the first frame is correct.
	// VOPI calcOptimalDistance() uses FOV math instead of view frustum,
	// so it can be called before drawAll().
	if (m_initial_rotation && m_mesh) {
		rotateCamera(v3f(m_custom_rot.X, m_custom_rot.Y, 0.f));
		calcOptimalDistance();
		m_initial_rotation = false;
	}
#endif

	cameraLoop();

	// Continuous rotation
	if (m_inf_rot)
		rotateCamera(v3f(0.f, -0.03f * (float)dtime_ms, 0.f));

	m_smgr->drawAll();

#if !IS_VOPI_ENGINE
	if (m_initial_rotation && m_mesh) {
		rotateCamera(v3f(m_custom_rot.X, m_custom_rot.Y, 0.f));
		calcOptimalDistance();
		m_initial_rotation = false;
	}
#endif

	m_driver->setViewPort(oldViewPort);
}

bool GUIScene::OnEvent(const SEvent &event)
{
	if (m_mouse_ctrl && event.EventType == EET_MOUSE_INPUT_EVENT) {
		if (event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN) {
			m_last_pos = v2f((f32)event.MouseInput.X, (f32)event.MouseInput.Y);
			return true;
		} else if (event.MouseInput.Event == EMIE_MOUSE_MOVED) {
			if (event.MouseInput.isLeftPressed()) {
				m_curr_pos = v2f((f32)event.MouseInput.X, (f32)event.MouseInput.Y);

				rotateCamera(v3f(
					m_last_pos.Y - m_curr_pos.Y,
					m_curr_pos.X - m_last_pos.X, 0.f));

				m_last_pos = m_curr_pos;
				return true;
			}
		}
	}

	return gui::IGUIElement::OnEvent(event);
}

void GUIScene::setStyles(const std::array<StyleSpec, StyleSpec::NUM_STATES> &styles)
{
	StyleSpec::State state = StyleSpec::STATE_DEFAULT;
	StyleSpec style = StyleSpec::getStyleFromStatePropagation(styles, state);

	setNotClipped(style.getBool(StyleSpec::NOCLIP, false));
	setBackgroundColor(style.getColor(StyleSpec::BGCOLOR, m_bgcolor));
}

/**
 * Sets the frame loop range for the mesh
 */
void GUIScene::setFrameLoop(f32 begin, f32 end)
{
	if (m_mesh->getStartFrame() != begin || m_mesh->getEndFrame() != end)
		m_mesh->setFrameLoop(begin, end);
}

/**
 * Sets the animation speed (FPS) for the mesh
 */
void GUIScene::setAnimationSpeed(f32 speed)
{
	m_mesh->setAnimationSpeed(speed);
}

/* Camera control functions */

#if IS_VOPI_ENGINE
inline void GUIScene::calcOptimalDistance()
{
	core::aabbox3df box = m_mesh->getBoundingBox();
	f32 width  = box.MaxEdge.X - box.MinEdge.X;
	f32 height = box.MaxEdge.Y - box.MinEdge.Y;
	f32 depth  = box.MaxEdge.Z - box.MinEdge.Z;
	f32 max_width = width > depth ? width : depth;

	// Calculate frustum slopes from FOV and aspect ratio directly,
	// avoiding dependency on view frustum which requires a prior drawAll().
	f32 fov = m_cam->getFOV();
	f32 v_slope = 2.0f * tanf(fov * 0.5f);
	core::recti rect = getAbsolutePosition();
	f32 aspect = (f32)rect.getWidth() / (f32)rect.getHeight();
	f32 h_slope = v_slope * aspect;

	f32 zoomX = rect.getWidth() / max_width;
	f32 zoomY = rect.getHeight() / height;
	f32 dist;

	if (zoomX < zoomY)
		dist = (max_width / h_slope) + (0.5f * max_width);
	else
		dist = (height / v_slope) + (0.5f * max_width);

	m_cam_distance = dist;
	m_update_cam = true;
#else
inline void GUIScene::calcOptimalDistance()
{
	core::aabbox3df box = m_mesh->getBoundingBox();
	f32 width  = box.MaxEdge.X - box.MinEdge.X;
	f32 height = box.MaxEdge.Y - box.MinEdge.Y;
	f32 depth  = box.MaxEdge.Z - box.MinEdge.Z;
	f32 max_width = width > depth ? width : depth;

	const scene::SViewFrustum *f = m_cam->getViewFrustum();
	f32 cam_far = m_cam->getFarValue();
	f32 far_width = core::line3df(f->getFarLeftUp(), f->getFarRightUp()).getLength();
	f32 far_height = core::line3df(f->getFarLeftUp(), f->getFarLeftDown()).getLength();

	core::recti rect = getAbsolutePosition();
	f32 zoomX = rect.getWidth() / max_width;
	f32 zoomY = rect.getHeight() / height;
	f32 dist;

	if (zoomX < zoomY)
		dist = (max_width / (far_width / cam_far)) + (0.5f * max_width);
	else
		dist = (height / (far_height / cam_far)) + (0.5f * max_width);

	m_cam_distance = dist;
	m_update_cam = true;
#endif
}

void GUIScene::updateCamera(scene::ISceneNode *target)
{
	m_target = target;
	updateTargetPos();

	m_last_target_pos = m_target_pos;
	updateCameraPos();

	m_update_cam = true;
}

void GUIScene::updateTargetPos()
{
	m_last_target_pos = m_target_pos;
	m_target->updateAbsolutePosition();
	m_target_pos = m_target->getAbsolutePosition();
}

void GUIScene::setCameraRotation(v3f rot)
{
	correctBounds(rot);

	core::matrix4 mat;
	mat.setRotationDegrees(rot);

	m_cam_pos = mat.rotateAndScaleVect(v3f(0.f, 0.f, m_cam_distance));

	m_cam_pos += m_target_pos;
	m_cam->setPosition(m_cam_pos);
	m_update_cam = false;
}

bool GUIScene::correctBounds(v3f &rot)
{
	const float ROTATION_MAX_1 = 60.0f;
	const float ROTATION_MAX_2 = 300.0f;

	// Limit and correct the rotation when needed
	if (rot.X < 90.f) {
		if (rot.X > ROTATION_MAX_1) {
			rot.X = ROTATION_MAX_1;
			return true;
		}
	} else if (rot.X < ROTATION_MAX_2) {
		rot.X = ROTATION_MAX_2;
		return true;
	}

	// Not modified
	return false;
}

void GUIScene::cameraLoop()
{
	updateCameraPos();
	updateTargetPos();

	if (m_target_pos != m_last_target_pos)
		m_update_cam = true;

	if (m_update_cam) {
		m_cam_pos = m_target_pos + (m_cam_pos - m_target_pos).normalize() * m_cam_distance;

		v3f rot = getCameraRotation();
		if (correctBounds(rot))
			setCameraRotation(rot);

		m_cam->setPosition(m_cam_pos);
		m_cam->setTarget(m_target_pos);

		m_update_cam = false;
	}
}
