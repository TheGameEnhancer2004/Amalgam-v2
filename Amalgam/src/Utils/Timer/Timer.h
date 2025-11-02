#pragma once

class Timer
{
private:
	float m_flLast = 0.f;

public:
	Timer();
	bool Check(float flS) const;
	bool Run(float flS);
	void Update();

	inline void operator-=(float flS)
    {
		m_flLast -= flS;
    }

	inline void operator+=(float flS)
    {
        m_flLast += flS;
    }
};