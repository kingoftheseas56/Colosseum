import QtQuick 2.15
import "GuideLogic.js" as Logic
import "GuideContentStart.js" as Start
import "GuideContentTankoban.js" as Tankoban
import "GuideContentBiblio.js" as Biblio
import "GuideContentTheatre.js" as Theatre
import "GuideContentHouse.js" as House

QtObject {
    id: root

    readonly property var allLessons: Start.lessons().concat(
        Tankoban.lessons(), Biblio.lessons(), Theatre.lessons(), House.lessons())
    readonly property var publishedLessons: Logic.visibleLessons(allLessons)

    function find(id) {
        return Logic.lessonById(publishedLessons, id);
    }

    function search(query, context) {
        return Logic.search(publishedLessons, query, context);
    }

    function section(id) {
        return Logic.lessonsForSection(publishedLessons, id);
    }

    Component.onCompleted: {
        var seen = {};
        for (var index = 0; index < allLessons.length; ++index) {
            var lesson = allLessons[index];
            var id = lesson && String(lesson.id || "").trim();
            var errors = Logic.validateLesson(lesson);
            if (errors.length > 0)
                console.warn("GuideCatalog invalid lesson '" + (id || "<missing id>")
                             + "': " + errors.join(", "));
            if (!id)
                continue;
            if (seen[id])
                console.warn("GuideCatalog duplicate lesson id: " + id);
            seen[id] = true;
        }
    }
}
