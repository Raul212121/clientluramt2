#pragma once

#include "../gamelib/ActorInstance.h"

class CMobPreviewTest
{
public:
	CMobPreviewTest();
	~CMobPreviewTest();

	void Toggle();
	void Show();
	void Hide();
	bool IsShow() const;

	bool Create(DWORD dwRace);
	void Destroy();
	void SetViewport(int x, int y, int width, int height);
	void Update();
	void Render();

private:
	bool m_isShow;
	DWORD m_dwRace;
	int m_viewX;
	int m_viewY;
	int m_viewWidth;
	int m_viewHeight;
	CActorInstance m_Actor;
};