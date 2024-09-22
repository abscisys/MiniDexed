#pragma once

#include "extra_features.h"

#include <circle/logger.h>
#include <circle/synchronize.h>
#include <fatfs/ff.h>
#include <circle/string.h>
#include <vector>
#include <memory>

class MiniLog
{
    DISALLOW_COPY_AND_ASSIGN(MiniLog);

public:
    static MiniLog& getInstance()
    {
        static MiniLog instance;
        return instance;
    }

    static void Log(const char* pMessage, ...)
    {
        va_list var;
        va_start(var, pMessage);

        CString message;
        message.FormatV(pMessage, var);

        
        MiniLog::getInstance().log(message);

        va_end(var);
    }
    
    void log(const CString& message)
    {
        this->m_spinLock.Acquire();
        this->m_buffer.push_back(message);
        this->m_spinLock.Release();

        this->serializeBuffer();
    }

private:
    MiniLog():
        m_file(new FIL())
    {
        FRESULT res = f_open(this->m_file, "SD:/minilog.txt", FA_WRITE | FA_CREATE_ALWAYS);
        assert(res == FR_OK);
    }

    ~MiniLog()
    {
        f_close(this->m_file);
    }

    void serializeBuffer()
    {
        if(this->m_file->err == 0)
        {
            this->m_spinLock.Acquire();
            std::vector<CString> tempBuffer = std::move(this->m_buffer);
            this->m_buffer.clear();
            this->m_spinLock.Release();

            if(!tempBuffer.empty())
            {
                FRESULT res;
                UINT nb = 0;
                for(const auto& message : tempBuffer)
                {
                    res = f_write(this->m_file, static_cast<const void*>((const char*)message), message.GetLength(), &nb);
                    assert(res == FR_OK);

                    res = f_write(this->m_file, "\n", 1, &nb);
                    assert(res == FR_OK);
                }
                res = f_sync(this->m_file);
                assert(res == FR_OK);
            }
        }
    }

    std::vector<CString> m_buffer;
    CSpinLock m_spinLock;
    FIL* m_file;
};