//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Hoe specific input handling
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "kbutton.h"
#include "input.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Hoe Input interface
//-----------------------------------------------------------------------------
class CHoeInput : public CInput
{
public:
};

static CHoeInput g_Input;

// Expose this interface
IInput *input = ( IInput * )&g_Input;
