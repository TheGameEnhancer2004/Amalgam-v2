#pragma once
#include "../Main/CUserCmd.h"
#include "../Misc/CGameEventListener.h"

class CHLTVCamera : CGameEventListener
{
public:
	int			m_nCameraMode; // current camera mode
	int			m_iCameraMan; // camera man entindex or 0
	Vector		m_vCamOrigin;  //current camera origin
	QAngle		m_aCamAngle;   //current camera angle
	int			m_iTraget1;	// first tracked target or 0
	int			m_iTraget2; // second tracked target or 0
	float		m_flFOV; // current FOV
	float		m_flOffset;  // z-offset from target origin
	float		m_flDistance; // distance to traget origin+offset
	float		m_flLastDistance; // too smooth distance
	float		m_flTheta; // view angle horizontal 
	float		m_flPhi; // view angle vertical
	float		m_flInertia; // camera inertia 0..100
	float		m_flLastAngleUpdateTime;
	bool		m_bEntityPacketReceived;	// true after a new packet was received
	int			m_nNumSpectators;
	char		m_szTitleText[64];
	CUserCmd	m_LastCmd;
	Vector		m_vecVelocity;
};