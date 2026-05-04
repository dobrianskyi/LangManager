#include "CommandRunner.h"

#include <QProcess>

QString runToolCommand(const QString &program, const QStringList &arguments, int startTimeoutMs, int finishTimeoutMs)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(startTimeoutMs)) {
        return QStringLiteral("Cannot start %1: %2").arg(program, process.errorString());
    }
    if (!process.waitForFinished(finishTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return QStringLiteral("Command timed out: %1 %2").arg(program, arguments.join(' '));
    }

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
    if (!errorOutput.trimmed().isEmpty()) {
        if (!output.endsWith(QLatin1Char('\n')) && !output.isEmpty()) {
            output.append(QLatin1Char('\n'));
        }
        output.append(errorOutput);
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (!output.endsWith(QLatin1Char('\n')) && !output.isEmpty()) {
            output.append(QLatin1Char('\n'));
        }
        output.append(QStringLiteral("[exit code %1]").arg(process.exitCode()));
    }
    return output.trimmed();
}

QString compactToolStatus(const QString &toolName, const QString &rawOutput)
{
    QString normalized = rawOutput;
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = normalized.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        constexpr qsizetype maxStatusLength = 140;
        if (trimmed.size() > maxStatusLength) {
            trimmed = trimmed.left(maxStatusLength - 3) + QStringLiteral("...");
        }
        return trimmed;
    }

    return QStringLiteral("%1 is installed.").arg(toolName);
}

QString compactToolTooltip(const QString &rawOutput)
{
    QString tooltip = rawOutput.trimmed();
    constexpr qsizetype maxTooltipLength = 4000;
    if (tooltip.size() > maxTooltipLength) {
        tooltip = tooltip.left(maxTooltipLength - 3) + QStringLiteral("...");
    }
    return tooltip;
}
