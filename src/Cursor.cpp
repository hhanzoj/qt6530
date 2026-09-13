#include <spl/debug.h>
#include <vt6530/Cursor.h>

Cursor::Cursor(int rows, int cols)
{
	m_row = 0;
	m_column = 0;
	m_numColumns = cols;
	m_numRows = rows;
}

Cursor::~Cursor()
{
}

void Cursor::Clear()
{
	m_row = 0;
	m_column = 0;
}

void Cursor::AdjustCol()
{
	if (m_column >= m_numColumns)
	{
		m_column = 0;
		m_row++;
		AdjustRow();
	}
	if (m_column < 0)
	{
		m_column = 0;
		m_row--;
		AdjustRow();
	}
}

void Cursor::AdjustRow()
{
	if (m_row >= m_numRows)
	{
		m_row = 0;
	}
	if (m_row < 0)
	{
		m_row = m_numRows - 1;
	}
}
